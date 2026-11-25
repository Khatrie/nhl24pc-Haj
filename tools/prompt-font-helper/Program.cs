using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Program
{
    private const int BlockSize = 0x40000;
    private static readonly byte[] DataHash = Convert.FromHexString("54ADEE065B8FCF7A6DD42634BC7905D1D3E60B780247EEE43843F1FB1AAFC54B");
    private static readonly byte[] IndexHash = Convert.FromHexString("8F3E04B560661213405598EAAB34A9EC142A2681B2143D014D51BEC82E59DE4F");
    private const long DataOffset = 533571226;
    private const int DataLength = 974338;
    private const long IndexOffset = 1989251;
    private const int IndexLength = 660;
    private static readonly Guid FontChunk = new("3d9b183e-f027-8147-f913-0340ad7d3783");

    private sealed class UnsupportedGameException : Exception
    {
        public UnsupportedGameException(string message) : base(message) { }
    }

    private sealed class Change
    {
        public long Offset { get; set; }
        public uint Original { get; set; }
        public uint Updated { get; set; }
    }

    private sealed class State
    {
        public string Layout { get; set; } = "";
        public List<Change> Changes { get; set; } = new();
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 4 || args[0] != "--game" || args[2] != "--layout" ||
                args[3] is not ("playstation" or "xbox" or "nintendo"))
                throw new ArgumentException("Usage: nhl24-font-patch --game <folder> --layout <playstation|xbox|nintendo>");
            Apply(Path.GetFullPath(args[1]), args[3]);
            return 0;
        }
        catch (UnsupportedGameException e)
        {
            Console.Error.WriteLine(e.Message);
            return 2;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e.Message);
            return 1;
        }
    }

    private static void Apply(string game, string layout)
    {
        string data = Path.Combine(game, "Data", "Gen4b");
        string tocPath = Path.Combine(data, "globals.toc");
        string casDir = Path.Combine(data, "superbundlelayout", "nhl_installpackage_00");
        string dataPath = Path.Combine(casDir, "cas_01.cas");
        string indexPath = Path.Combine(casDir, "cas_02.cas");
        string stateDir = Path.Combine(game, ".nhl24-prompt-font");
        if (!Directory.Exists(game))
            throw new UnsupportedGameException("The game folder was not found. No changes were made.");
        if (!Directory.Exists(stateDir) && layout == "playstation") return;
        Directory.CreateDirectory(stateDir);
        using var processLock = new FileStream(Path.Combine(stateDir, "lock"), FileMode.OpenOrCreate,
            FileAccess.ReadWrite, FileShare.None);
        string pendingPath = Path.Combine(stateDir, "pending.json");
        string statePath = Path.Combine(stateDir, "state.json");
        if (Process.GetProcessesByName("nhl24").Length != 0)
        {
            if (!File.Exists(pendingPath) && !File.Exists(statePath) && layout == "playstation") return;
            if (!File.Exists(pendingPath) && File.Exists(statePath))
            {
                State current = ReadState(statePath);
                if (current.Layout == layout && CurrentMatches(tocPath, current, true)) return;
            }
            if (!File.Exists(pendingPath) && !File.Exists(statePath) && layout != "playstation")
            {
                if (!File.Exists(tocPath) || !File.Exists(dataPath) || !File.Exists(indexPath))
                    throw new UnsupportedGameException("The game font archives were not found. No changes were made.");
                ReadChecked(dataPath, DataOffset, DataLength, DataHash);
                ReadChecked(indexPath, IndexOffset, IndexLength, IndexHash);
            }
            throw new InvalidOperationException("Close NHL 24 before changing its controller prompts.");
        }
        Recover(tocPath, pendingPath);

        if (File.Exists(statePath))
        {
            State previous = ReadState(statePath);
            if (previous.Layout == layout && CurrentMatches(tocPath, previous, true))
                return;
            Restore(tocPath, previous);
            File.Delete(statePath);
        }
        if (layout == "playstation")
            return;

        if (!File.Exists(tocPath) || !File.Exists(dataPath) || !File.Exists(indexPath))
            throw new UnsupportedGameException("The game font archives were not found. No changes were made.");

        byte[] dataStored = ReadChecked(dataPath, DataOffset, DataLength, DataHash);
        byte[] indexStored = ReadChecked(indexPath, IndexOffset, IndexLength, IndexHash);
        byte[] toc = File.ReadAllBytes(tocPath);
        int dataField = FindTocEntry(toc, 1, (uint)DataOffset, DataLength);
        int indexField = FindTocEntry(toc, 2, (uint)IndexOffset, IndexLength);
        byte[] raw = Decode(dataStored);
        byte[] index = Decode(indexStored);
        UpdateFont(raw, index, layout);
        byte[] newData = Store(raw);
        byte[] newIndex = Store(index);
        long dataEnd = new FileInfo(dataPath).Length;
        long indexEnd = new FileInfo(indexPath).Length;
        long dataAt = (dataEnd + 15) & ~15L;
        long indexAt = (indexEnd + 15) & ~15L;
        if (dataAt + newData.Length > uint.MaxValue || indexAt + newIndex.Length > uint.MaxValue)
            throw new InvalidOperationException("The game archive is too large for the font update.");
        var next = new State { Layout = layout, Changes = new List<Change> {
            new() { Offset = dataField, Original = (uint)DataOffset, Updated = (uint)dataAt },
            new() { Offset = dataField + 4, Original = DataLength, Updated = (uint)newData.Length },
            new() { Offset = indexField, Original = (uint)IndexOffset, Updated = (uint)indexAt },
            new() { Offset = indexField + 4, Original = IndexLength, Updated = (uint)newIndex.Length }
        }};
        WriteState(pendingPath, next);
        Append(dataPath, dataEnd, dataAt, newData);
        Append(indexPath, indexEnd, indexAt, newIndex);
        WriteToc(tocPath, next, false);
        File.Move(pendingPath, statePath);
    }

    private static byte[] ReadChecked(string path, long offset, int length, byte[] expected)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (offset + length > file.Length) throw new UnsupportedGameException("The installed game version is not supported by the font tool.");
        file.Position = offset;
        var bytes = new byte[length];
        file.ReadExactly(bytes);
        if (!SHA256.HashData(bytes).AsSpan().SequenceEqual(expected))
            throw new UnsupportedGameException("The font archive differs from the verified NHL 24 v1.00 data. No changes were made.");
        return bytes;
    }

    private static int FindTocEntry(byte[] toc, uint cas, uint offset, int length)
    {
        int found = -1;
        for (int i = 4; i + 8 <= toc.Length; i++)
        {
            if (BinaryPrimitives.ReadUInt32BigEndian(toc.AsSpan(i - 4)) != cas ||
                BinaryPrimitives.ReadUInt32BigEndian(toc.AsSpan(i)) != offset ||
                BinaryPrimitives.ReadUInt32BigEndian(toc.AsSpan(i + 4)) != (uint)length) continue;
            if (found >= 0) throw new InvalidDataException("The font archive index has ambiguous entries.");
            found = i;
        }
        return found >= 0 ? found : throw new UnsupportedGameException("The game font index was not found. No changes were made.");
    }

    private static byte[] Decode(byte[] stored)
    {
        using var output = new MemoryStream();
        string temp = Path.Combine(Path.GetTempPath(), "nhl24-font-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        try
        {
            int p = 0;
            while (p < stored.Length)
            {
                if (p + 8 > stored.Length) throw new InvalidDataException("Truncated archive block.");
                int rawSize = (int)(BinaryPrimitives.ReadUInt32BigEndian(stored.AsSpan(p)) & 0xffffff);
                ushort code = BinaryPrimitives.ReadUInt16BigEndian(stored.AsSpan(p + 4));
                int size = BinaryPrimitives.ReadUInt16BigEndian(stored.AsSpan(p + 6)) | ((code & 15) << 16);
                p += 8;
                if (rawSize <= 0 || rawSize > BlockSize || size <= 0 || p + size > stored.Length)
                    throw new InvalidDataException("Invalid archive block.");
                if ((code >> 8) == 0)
                {
                    if (size != rawSize) throw new InvalidDataException("Invalid uncompressed block.");
                    output.Write(stored, p, size);
                }
                else if ((code >> 8) == 0x11)
                {
                    string input = Path.Combine(temp, "input.bin");
                    string result = Path.Combine(temp, "output.bin");
                    using (var file = File.Create(input))
                    {
                        using var writer = new BinaryWriter(file);
                        writer.Write((long)rawSize);
                        writer.Write(stored, p, size);
                    }
                    var start = new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "ooz.exe")) {
                        UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true
                    };
                    start.ArgumentList.Add("-f");
                    start.ArgumentList.Add(input);
                    start.ArgumentList.Add(result);
                    using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start the archive decoder.");
                    string error = process.StandardError.ReadToEnd();
                    process.WaitForExit();
                    if (process.ExitCode != 0) throw new InvalidDataException("Archive decompression failed: " + error);
                    byte[] decoded = File.ReadAllBytes(result);
                    if (decoded.Length != rawSize) throw new InvalidDataException("The archive decoder returned an incorrect size.");
                    output.Write(decoded);
                }
                else throw new InvalidDataException("Unsupported archive compression.");
                p += size;
            }
            return output.ToArray();
        }
        finally { Directory.Delete(temp, true); }
    }

    private static void UpdateFont(byte[] raw, byte[] index, string layout)
    {
        int count = BinaryPrimitives.ReadInt32LittleEndian(index.AsSpan(12));
        long start = BinaryPrimitives.ReadInt64LittleEndian(index.AsSpan(16));
        if (count <= 0 || count > 10000 || start < 0 || start + count * 56L > index.Length)
            throw new InvalidDataException("Unsupported font index.");
        bool found = false;
        for (int n = 0; n < count; n++)
        {
            int entry = checked((int)start + n * 56);
            if (new Guid(index.AsSpan(entry + 40, 16)) != FontChunk) continue;
            long nameAt = BinaryPrimitives.ReadInt64LittleEndian(index.AsSpan(entry));
            long offset = BinaryPrimitives.ReadInt64LittleEndian(index.AsSpan(entry + 24));
            long size = BinaryPrimitives.ReadInt64LittleEndian(index.AsSpan(entry + 32));
            if (nameAt < 0 || nameAt >= index.Length || offset < 0 || size <= 0 || offset + size > raw.Length)
                throw new InvalidDataException("Invalid font index entry.");
            int end = Array.IndexOf(index, (byte)0, (int)nameAt);
            if (end < 0) throw new InvalidDataException("Invalid font name.");
            string name = System.Text.Encoding.UTF8.GetString(index, (int)nameAt, end - (int)nameAt);
            if (name == "nhl/ui/fonts/nhl-symbols.ttf")
            {
                byte[] font = raw.AsSpan((int)offset, (int)size).ToArray();
                byte[] replacement = PromptFontMapper.Remap(font, layout);
                if (replacement.Length != font.Length) throw new InvalidDataException("The remapped font changed size.");
                replacement.CopyTo(raw, (int)offset);
                found = true;
            }
            long storedStart = offset / BlockSize * (BlockSize + 8L);
            long storedEnd = ((offset + size - 1) / BlockSize + 1) * (BlockSize + 8L);
            BinaryPrimitives.WriteInt64LittleEndian(index.AsSpan(entry + 8), storedStart);
            BinaryPrimitives.WriteInt64LittleEndian(index.AsSpan(entry + 16), storedEnd);
        }
        if (!found) throw new InvalidDataException("The game symbol font was not found.");
    }

    private static byte[] Store(byte[] raw)
    {
        using var output = new MemoryStream();
        Span<byte> header = stackalloc byte[8];
        for (int p = 0; p < raw.Length; p += BlockSize)
        {
            int n = Math.Min(BlockSize, raw.Length - p);
            BinaryPrimitives.WriteUInt32BigEndian(header, (uint)n);
            BinaryPrimitives.WriteUInt16BigEndian(header[4..], (ushort)(0x70 | (n >> 16)));
            BinaryPrimitives.WriteUInt16BigEndian(header[6..], (ushort)n);
            output.Write(header);
            output.Write(raw, p, n);
        }
        return output.ToArray();
    }

    private static void Append(string path, long expected, long offset, byte[] bytes)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        if (file.Length != expected) throw new IOException("The game archive changed during the font update.");
        file.Position = file.Length;
        file.Write(new byte[checked((int)(offset - expected))]);
        file.Write(bytes);
        file.Flush(true);
    }

    private static State ReadState(string path) => JsonSerializer.Deserialize<State>(File.ReadAllText(path))
        ?? throw new InvalidDataException("Invalid font update journal.");

    private static void WriteState(string path, State state)
    {
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                JsonSerializer.Serialize(file, state);
                file.Flush(true);
            }
            File.Move(temp, path);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }

    private static void Recover(string toc, string journal)
    {
        if (!File.Exists(journal)) return;
        Restore(toc, ReadState(journal));
        File.Delete(journal);
    }

    private static bool CurrentMatches(string toc, State state, bool updated)
    {
        byte[] bytes = File.ReadAllBytes(toc);
        return state.Changes.Count == 4 && state.Changes.All(c => c.Offset >= 0 && c.Offset + 4 <= bytes.Length &&
            BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan((int)c.Offset)) == (updated ? c.Updated : c.Original));
    }

    private static void Restore(string toc, State state) => WriteToc(toc, state, true);

    private static void WriteToc(string path, State state, bool restore)
    {
        if (state.Changes.Count != 4) throw new InvalidDataException("Invalid font update journal.");
        byte[] original = File.ReadAllBytes(path);
        byte[] updated = (byte[])original.Clone();
        foreach (Change change in state.Changes)
        {
            if (change.Offset < 0 || change.Offset + 4 > original.Length) throw new InvalidDataException("Invalid font update journal.");
            Span<byte> value = updated.AsSpan((int)change.Offset, 4);
            uint current = BinaryPrimitives.ReadUInt32BigEndian(value);
            if (current != change.Original && current != change.Updated)
                throw new IOException("The game index was changed by another tool. Font update was stopped.");
            BinaryPrimitives.WriteUInt32BigEndian(value, restore ? change.Original : change.Updated);
        }
        string temp = path + ".nhl24-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                file.Write(updated);
                file.Flush(true);
            }
            if (!File.ReadAllBytes(path).AsSpan().SequenceEqual(original))
                throw new IOException("The game index changed during the font update.");
            File.Replace(temp, path, null);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
}

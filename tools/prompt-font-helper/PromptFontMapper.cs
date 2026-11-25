using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class PromptFontMapper
{
    private const uint ChecksumMagic = 0xB1B0AFBA;
    private static readonly string[] NintendoShoulderGlyphs = {
        "000200dcff7909f6061300030009000017112111012135211123dc091af9a20348fda8f087069af966019af003c0",
        "000300dcff7909f606130003000f0015000017112111013311331321033737272721172117150721dc091af9a2f09ccc0120f0a8483078fd30f0012c3c3cfed487069af966019a01e0fe20021c78b4f078f03c783c",
        "000300b9ff1507a306a90003000d001300001711211101213521013521152101052135211123b906eaf9fc0253fe82017efdad017ffe8102ee0253fe57aaeb0794f86c019a9902209999fde099aa02a8",
        "000400b9ff1507a306a90003000d0019001f00001711211101213521013521152101053311331333033737272721173317150723b906eaf9fc0253fe82017efdad017ffe8102eeaa6f90ccaa77332255fe02aad52a2ad5eb0794f86c019a9902209999fde0990154feac017f5480aa55aa2b542b"
    };

    private readonly record struct Table(int Record, int Offset, int Length);

    public static byte[] Remap(byte[] source, string layout)
    {
        if (source == null) throw new ArgumentNullException(nameof(source));
        if (layout != "playstation" && layout != "xbox" && layout != "nintendo")
            throw new ArgumentException("Unknown controller prompt layout.", nameof(layout));
        var data = (byte[])source.Clone();
        if (layout == "playstation") return data;

        ushort Read16(int offset)
        {
            if (offset < 0 || offset > data.Length - 2) throw new InvalidDataException("Truncated font.");
            return BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2));
        }
        uint Read32(int offset)
        {
            if (offset < 0 || offset > data.Length - 4) throw new InvalidDataException("Truncated font.");
            return BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(offset, 4));
        }
        void Write16(int offset, ushort value) => BinaryPrimitives.WriteUInt16BigEndian(data.AsSpan(offset, 2), value);
        void Write32(int offset, uint value) => BinaryPrimitives.WriteUInt32BigEndian(data.AsSpan(offset, 4), value);
        int CheckedOffset(uint value)
        {
            if (value > int.MaxValue) throw new InvalidDataException("Invalid font offset.");
            return (int)value;
        }
        if (Read32(0) != 0x00010000) throw new InvalidDataException("Expected a TrueType font.");
        int count = Read16(4);
        var tables = new Dictionary<string, Table>();
        for (int i = 0; i < count; i++)
        {
            int record = checked(12 + i * 16);
            Read32(record + 12);
            string name = Encoding.ASCII.GetString(data, record, 4);
            int offset = CheckedOffset(Read32(record + 8));
            int length = CheckedOffset(Read32(record + 12));
            if (offset > data.Length || length > data.Length - offset)
                throw new InvalidDataException("Invalid font table range.");
            if (!tables.TryAdd(name, new Table(record, offset, length)))
                throw new InvalidDataException("Duplicate font table.");
        }
        if (!tables.TryGetValue("cmap", out var cmap) || !tables.TryGetValue("head", out var head) ||
            !tables.TryGetValue("glyf", out var glyf) || !tables.TryGetValue("loca", out var loca) ||
            !tables.TryGetValue("maxp", out var maxp))
            throw new InvalidDataException("Required font tables are missing.");
        if (head.Length < 54 || maxp.Length < 6 || cmap.Length < 4)
            throw new InvalidDataException("Required font tables are truncated.");

        var unicodeMaps = new HashSet<int>();
        int encodingCount = Read16(cmap.Offset + 2);
        if (4L + 8L * encodingCount > cmap.Length) throw new InvalidDataException("Invalid cmap records.");
        for (int i = 0; i < encodingCount; i++)
        {
            int record = cmap.Offset + 4 + 8 * i;
            int platform = Read16(record);
            int encoding = Read16(record + 2);
            if (platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10))) continue;
            int relative = CheckedOffset(Read32(record + 4));
            if (relative > cmap.Length - 4) throw new InvalidDataException("Invalid cmap offset.");
            int subtable = cmap.Offset + relative;
            if (Read16(subtable) != 4) throw new InvalidDataException("Unsupported Unicode cmap format.");
            int size = Read16(subtable + 2);
            if (size < 16 || size > cmap.Length - relative) throw new InvalidDataException("Invalid cmap size.");
            unicodeMaps.Add(subtable);
        }
        if (unicodeMaps.Count == 0) throw new InvalidDataException("No supported Unicode cmap.");

        int GlyphSlot(int subtable, int codepoint)
        {
            int size = Read16(subtable + 2);
            int segments = Read16(subtable + 6) / 2;
            if (segments == 0 || 16L + segments * 8L > size)
                throw new InvalidDataException("Invalid cmap segments.");
            int ends = subtable + 14;
            int starts = ends + 2 * segments + 2;
            int deltas = starts + 2 * segments;
            int ranges = deltas + 2 * segments;
            for (int i = 0; i < segments; i++)
            {
                if (codepoint < Read16(starts + i * 2) || codepoint > Read16(ends + i * 2)) continue;
                int rangePosition = ranges + i * 2;
                int rangeOffset = Read16(rangePosition);
                if (rangeOffset == 0 || Read16(deltas + i * 2) != 0)
                    throw new InvalidDataException("Unsupported cmap segment.");
                long slot = (long)rangePosition + rangeOffset + 2L * (codepoint - Read16(starts + i * 2));
                if (slot < subtable || slot + 2 > (long)subtable + size)
                    throw new InvalidDataException("Invalid cmap glyph slot.");
                return (int)slot;
            }
            throw new InvalidDataException("Button glyph is absent from font.");
        }

        ushort[]? original = null;
        int[] targets = layout == "xbox"
            ? new[] { 0xf090, 0xf091, 0xf092, 0xf093, 0xf094, 0xf095, 0xf096, 0xf097 }
            : new[] { 0xf050, 0xf051, 0xf052, 0xf053, 0xf095, 0xf094, 0xf097, 0xf096 };
        int glyphCount = Read16(maxp.Offset + 4);
        foreach (int subtable in unicodeMaps)
        {
            var current = new ushort[8];
            var replacement = new ushort[8];
            var slots = new int[8];
            for (int i = 0; i < 8; i++)
            {
                slots[i] = GlyphSlot(subtable, 0xf050 + i);
                current[i] = Read16(slots[i]);
                replacement[i] = Read16(GlyphSlot(subtable, targets[i]));
                if (current[i] == 0 || current[i] >= glyphCount || replacement[i] == 0 || replacement[i] >= glyphCount)
                    throw new InvalidDataException("Invalid button glyph index.");
            }
            if (original == null) original = current;
            else for (int i = 0; i < 8; i++)
                if (original[i] != current[i]) throw new InvalidDataException("Unicode maps disagree.");
            for (int i = 0; i < 8; i++) Write16(slots[i], replacement[i]);
        }

        if (layout == "nintendo")
        {
            if (original == null) throw new InvalidDataException("No Unicode font mapping was found.");
            int locaFormat = Read16(head.Offset + 50);
            if (locaFormat != 0 && locaFormat != 1) throw new InvalidDataException("Unsupported glyph locations.");
            int entrySize = locaFormat == 0 ? 2 : 4;
            if ((long)(glyphCount + 1) * entrySize > loca.Length) throw new InvalidDataException("Truncated glyph locations.");
            int GlyphOffset(int index) => locaFormat == 0
                ? Read16(loca.Offset + index * 2) * 2
                : CheckedOffset(Read32(loca.Offset + index * 4));
            var used = new HashSet<ushort>();
            for (int i = 0; i < 4; i++)
            {
                if (!used.Add(original[i])) throw new InvalidDataException("Shoulder glyphs are shared.");
                int start = GlyphOffset(original[i]);
                int end = GlyphOffset(original[i] + 1);
                byte[] glyph = Convert.FromHexString(NintendoShoulderGlyphs[i]);
                if (start < 0 || end < start || end > glyf.Length || glyph.Length > end - start)
                    throw new InvalidDataException("Shoulder glyph does not fit in this font.");
                Array.Clear(data, glyf.Offset + start, end - start);
                Array.Copy(glyph, 0, data, glyf.Offset + start, glyph.Length);
            }
        }

        uint Sum(int offset, int length)
        {
            uint sum = 0;
            for (int i = 0; i < length; i += 4)
            {
                uint word = 0;
                for (int j = 0; j < 4; j++) word = (word << 8) | (i + j < length ? data[offset + i + j] : 0u);
                sum = unchecked(sum + word);
            }
            return sum;
        }
        Write32(head.Offset + 8, 0);
        Write32(cmap.Record + 4, Sum(cmap.Offset, cmap.Length));
        if (layout == "nintendo") Write32(glyf.Record + 4, Sum(glyf.Offset, glyf.Length));
        Write32(head.Record + 4, Sum(head.Offset, head.Length));
        Write32(head.Offset + 8, unchecked(ChecksumMagic - Sum(0, data.Length)));
        if (Sum(0, data.Length) != ChecksumMagic) throw new InvalidDataException("Font checksum failed.");
        return data;
    }
}

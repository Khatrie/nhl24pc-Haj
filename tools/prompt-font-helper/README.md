# Controller prompt font helper

Build with `powershell -NoProfile -ExecutionPolicy Bypass -File tools/prompt-font-helper/build.ps1` from the repository root. The output is written to `build/prompt-font-helper` and contains the self-contained helper, `ooz.exe` and its source and license.

Run `nhl24-font-patch.exe --game <game-folder> --layout <playstation|xbox|nintendo>` with NHL 24 closed. The helper returns 0 on success, 2 when the game's font archive does not match the supported data, and 1 for other errors. The standalone selector and launcher call the same helper.

The helper validates the original compressed font chunks before writing. Its current fingerprint was captured from CUSA37934 version 01.00. Other installations can use it if their chunks match. It journals the index update, recovers unfinished updates, and atomically replaces the index file. PlayStation restores the original index references. The tool retains appended archive data to avoid truncating changes made by other tools, so each change to a different non-PlayStation layout adds roughly 3.3 MB.

`ooz.exe` is built from Powzix's ooz, a GPL-3.0-or-later Kraken decompressor. Its source and GPL license are in `ooz/` and are included beside the release helper. The helper uses no proprietary Oodle runtime.

# Legacy tables for earlier Android save states

Android versions before v0.11.9 saved their save states with a format 2
symbols record. That record could not name the words that point into the
program's read-only data (texts) or the page addresses that threads keep in
registers. This version can still read such a state. It names those words
again at load time, using a table of the version that saved the state
(`port/platform/state_legacy.h`, issue 217).

Android versions from v0.11.9 on save format 3 records, which name those
words themselves, but their functions' code hashes are of the rules of their
day (rules 2 up to v0.11.15). Rules 2 counted the linker's erratum 843419
patch as code and lost some page registers, so a function the linker patched
in only one of two builds hashed apart and its states took their checkpoint
(every v0.11.15 state saved in a room with birds, in v0.11.16-rc8). A table of
such a version (`record 3`) holds its symbol file and its functions' code
hashes under rules 2 and the current rules: the load checks the record's
hashes against the first and compares the second with this build's.

The tables are made by `tools/state_legacy_table.py build` from the released
APK. Each format 2 table is a folder holding these files, each under the
repository's 256 KiB file cap:

- the version's own symbol file;
- its sections;
- its read-only text runs, each marked when it starts a literal (the byte
  before it is zero, or it is the first byte of `.rodata`). The table holds
  only the runs' bytes, so the loader cannot see the byte before a run. The
  manifest's `text_index 2` names this format and `lstarts` counts the
  marked runs. The game refuses a table without `text_index 2` and says it
  must be rebuilt;
- its functions' code hashes;
- every ADRP's verdict (`carry.z`): whether a paused thread can hold the page
  it makes, judged on the version's own instructions (an ADD's immediate,
  which the code hash masks, decides whether it copies the page). When the
  ADRPs a state names make two pages in this build, the load keeps only
  those.

A format 3 table (`record 3`, `hash_verify_rules 2` in its manifest) holds the
code hashes and the ADRPs' verdicts alone, about 170 KB (`build
--hashes-only`). The newest release's
also keeps its symbol file (about 240 KB more): the package gate names the
functions by it. `check` enforces both.

`index.txt` lists the tables. Each line gives the platform, the kind, the
folder and the commit text that the version's states carry.

| Table | Kind | Source APK | SHA-256 of the APK | States' commit |
| --- | --- | --- | --- | --- |
| `v0.11.7-android` | release | `ico-pc-v0.11.7-android.apk` | `334f04819472b1353de45e0b0cfd52f103252fe50226f4df793f2baed3c2939a` | `v0.11.6-183-g66d9d0dcba` |
| `v0.11.8-android` | release | `ico-pc-v0.11.8-android.apk` | `7b474d9be4e9fece3ebed6fbc199d00c652f0f7b279646c828c3c8704a572485` | `v0.11.7-68-gaeb00e6100` |
| `v0.11.8-rc4-android` | rc | `ico-pc-v0.11.8-rc4-android.apk` | `10e43ab4ff7f2d93ab3a050e508ea7ead33494bf48c7c5cc560ba9c0b16afa3a` | `v0.11.7-66-gaf8507442c` |
| `v0.11.9-android` | release, record 3 | `ico-pc-v0.11.9-android.apk` | `9c154b381b75cdd14bccf897183aaebf392b1ffb7d5f8374ae4f0a6e574f6bd0` | `v0.11.8-68-g96fd7c099a` |
| `v0.11.10-android` | release, record 3 | `ico-pc-v0.11.10-android.apk` | `e8c33123ef238ca24bb2d37b456af8c3bd9a34329e40daeb45504df6ad337fc9` | `v0.11.9-6-ge9bcc9d3ef` |
| `v0.11.11-android` | release, record 3 | `ico-pc-v0.11.11-android.apk` | `9d4c5fd0853166b785a700b4c63c8fd5dbede13f19a8b91766d5d9f06e6e05b1` | `v0.11.10-23-g4832c07665` |
| `v0.11.12-android` | release, record 3 | `ico-pc-v0.11.12-android.apk` | `bbed93af3b06a575bb7021d87ddc28c365d999e61f0a37918c814e0c7a5a5f9c` | `v0.11.11-29-ga42959af67` |
| `v0.11.13-android` | release, record 3 | `ico-pc-v0.11.13-android.apk` | `f7a4ebeb50e436c74ae69d97e99c272995ba7999d6f3f3c89eb1439aa118c821` | `v0.11.12-69-g0c585e991a` |
| `v0.11.14-android` | release, record 3 | `ico-pc-v0.11.14-android.apk` | `13b2b65d24aa0c019890262c8befa21ddf505743982b1f1041854567dae5634e` | `v0.11.13-42-g0aaa7ac704` |
| `v0.11.15-rc6-android` | rc, record 3 | `ico-pc-v0.11.15-rc6-android.apk` | `e700519208ecd4b09ba0fff39ba849821674f8bf472fd85f3c913142db249444` | `v0.11.14-58-g5ccc7f8365` |
| `v0.11.15-android` | release, record 3 | `ico-pc-v0.11.15-android.apk` | `2efb30eea372cbc52b2a6dde95d101ec3bf9f79507d6260878cd019b0836fc36` | `v0.11.14-94-g5396fcfcd2` |

Every table holds code hashes of rules 3. The release APKs of v0.11.7 to
v0.11.13 came from the project's GitHub release pages (v0.11.7's and v0.11.8's
SHA-256 equal the ones their first tables recorded; rebuilt, their other files
came out byte for byte the same). `v0.11.15-rc6-android` is the release
candidate the owner's test states came from; like rc4's it ships in release
candidate APKs only.

## Which versions have a table

A version gets a table only when its game data layout hash equals this
version's (`0x7e3463baa76b43a8`): v0.11.7 and every later release qualify
(each release package from v0.11.9 to v0.11.15 passed the layout gate under
that hash).
Versions up to v0.11.6 have another layout, so their states are refused for
that reason whether or not a table exists.

`layout.txt` records that hash. `tools/package_android.sh` fails an Android package whose
layout hash differs (`tools/android_syms_lines.py layout`).

`layout-windows-x64.txt` lists the game's data sections of the Windows build whose hash
`layout.txt` records (`layout-windows-x64`): the range's start, the largest alignment, and each
object's section with its offset and size (`tools/android_syms_lines.py layout-rows` on that
build's link map). When `tools/package_win.sh` finds another hash, it compares the new map with
this list and names the cause in its log. Write the list again whenever that hash is recorded
again.

## Release candidates

A release candidate's table, such as rc4's, is packaged only into release
candidate APKs (labels ending in `-rcN`, `android/app/build.gradle`). A
release APK leaves it out.

## Checks

The `state_legacy_tables` test checks the files against their manifests
(sizes, SHA-256, counts, address order, the text index's format and its
literal-start count, the code hash rules). The `state_legacy` test loads every
table with the game's own loader. The `state_legacy_gate` test runs the
package gate over a fake pair of builds.

## Making a table

```
python3 -I tools/state_legacy_table.py build --apk <the release APK> \
    --commit <the commit text its states carry> --name <version>-android [--kind rc] \
    [--record 3 [--hashes-only]]
```

`--record 3` for v0.11.9 and later. The commit text is the one the library
holds (`git describe` of the build's commit, as `v0.11.14-94-g5396fcfcd2`).

A table must be rebuilt whenever the code hash rules change
(`port/platform/code_hash.h` `ICO_CODE_HASH_RULES`, `tools/code_hash_a64.py`)
or the text index's format changes (`ICO_LEGACY_TEXT_INDEX` in
`port/platform/state_legacy.h`, `TEXT_INDEX` in the tool). One whose APK is
gone keeps its rules with a `hash_rules_kept <why>` line in its manifest
(`check` refuses an older rules' table without one).

After every Android release, add its `--record 3` table: the next release's
package gate compares with it, and its states compare through it when the
rules change again. The previous newest release's table then drops its symbol
file (`build --hashes-only` again); each table adds about 170 KB to the
APK.

## The package gate on the game's code

`tools/package_android.sh` runs `tools/state_legacy_table.py gate` after the
layout gate. It hashes this build's game functions (the link map's `ico2/`
and `ico/` objects) that can hold a paused frame (they reach
`ico_fiber_yield` or make an indirect call) and compares them with the newest
release table's hashes. One that differs while its source file did not change
since that release (`git diff`) fails the package with its name and both
hashes: that release's states saved inside it would go to their checkpoint
for a change of the build, not of the game.

What to do: find what differs. A linker patch or an instruction the rules
misread is a gap in the hash rules; fix it in `port/platform/code_hash.c` and
`tools/code_hash_a64.py` together (their shared corpus,
`tools/test/code_hash_a64_corpus.txt`, and the `code_hash_corpus` test keep
them equal) and rebuild the tables. Only a game code change made on purpose
with no source change (a compiler or flag change) goes in `code_changes.txt`
as `changed <table> <function> <hash>`, as `layout.txt` records a layout
change made on purpose, knowing those states then take the checkpoint path.

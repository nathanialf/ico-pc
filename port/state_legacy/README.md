# Legacy tables for earlier Android save states

Android versions before v0.11.9 saved their save states with a format 2
symbols record. That record could not name the words that point into the
program's read-only data (texts) or the page addresses that threads keep in
registers. This version can still read such a state. It names those words
again at load time, using a table of the version that saved the state
(`port/platform/state_legacy.h`, issue 217).

The tables are made by `tools/state_legacy_table.py build` from the released
APK. Each table is a folder holding these files, each under the
repository's 256 KiB file cap:

- the version's own symbol file;
- its sections;
- its read-only text runs;
- its functions' code hashes.

`index.txt` lists the tables. Each line gives the platform, the kind, the
folder and the commit text that the version's states carry.

| Table | Kind | Source APK | SHA-256 of the APK | States' commit |
| --- | --- | --- | --- | --- |
| `v0.11.7-android` | release | `ico-pc-v0.11.7-android.apk` | `334f04819472b1353de45e0b0cfd52f103252fe50226f4df793f2baed3c2939a` | `v0.11.6-183-g66d9d0dcba` |
| `v0.11.8-android` | release | `ico-pc-v0.11.8-android.apk` | `7b474d9be4e9fece3ebed6fbc199d00c652f0f7b279646c828c3c8704a572485` | `v0.11.7-68-gaeb00e6100` |
| `v0.11.8-rc4-android` | rc | `ico-pc-v0.11.8-rc4-android.apk` | `10e43ab4ff7f2d93ab3a050e508ea7ead33494bf48c7c5cc560ba9c0b16afa3a` | `v0.11.7-66-gaf8507442c` |

## Which versions have a table

A version gets a table only when its game data layout hash equals this
version's (`0x7e3463baa76b43a8`). Only v0.11.7 and v0.11.8 qualify. Versions
up to v0.11.6 have another layout, so their states are refused for that
reason whether or not a table exists.

## Release candidates

A release candidate's table, such as rc4's, is packaged only into release
candidate APKs (labels ending in `-rcN`, `android/app/build.gradle`). A
release APK leaves it out.

## Checks

The `state_legacy_tables` test checks the files against their manifests
(sizes, SHA-256, counts, address order). The `state_legacy` test loads every
table with the game's own loader.

## Making a table

```
python3 -I tools/state_legacy_table.py build --apk <the release APK> \
    --commit <the commit text its states carry> --name <version>-android [--kind rc]
```

A table must be rebuilt whenever the code hash rules change
(`port/platform/code_hash.h` `ICO_CODE_HASH_RULES`, `tools/code_hash_a64.py`).

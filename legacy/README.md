# Legacy: Pedalboard2 leftovers

What shipped with Pedalboard2 (Niall Moody, 2013) that isn't part of Pedalboard 3, moved here from the repository root (2026-09-14) so the root holds only the v3 project. Kept for reference; nothing builds from it.

- `documentation/`: Pedalboard2's end-user documentation (HTML and images), the behavioural reference for v2 features.
- `readme.txt`: Pedalboard2's readme.
- `ToDo.txt`: Pedalboard2's to-do list.

Pedalboard2's source code stays at the root in `source code/` (read-only), where the v3 plans and the v2 file importer (`app-v3/src/session/v2_import/`) point.

## Why the Pedalboard2 binaries aren't here

`Pedalboard2.exe` and `Pedalboard2-64bit.exe` were removed on 2026-10-13, before this repository goes public. They are someone else's GPLv3 binaries, and redistributing them obliges us to offer the corresponding source **for those exact builds** — `source code/` is Pedalboard2's source, but nothing establishes it is what produced them. Keeping two binaries we can't make that promise about, to save a download, was the wrong trade.

They remain in this repository's history, and Pedalboard2 is still published by its author at <https://www.niallmoody.com/work/pedalboard2/>. For parity work, `documentation/` is the better reference anyway: it says what v2 was meant to do, rather than only what one build of it did.

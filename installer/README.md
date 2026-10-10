# AdMutify installer

Built with Inno Setup 6 by `build.bat`; the result is `dist\AdMutify-Setup-<version>.exe`.

## Install modes

The setup asks first whether to install for all users (needs admin rights) or only for the current user. The folder page has a "Portable installation" checkbox: it installs into a folder next to the setup, writes `portable.txt` and creates no Start menu entry, no registry entries and no uninstaller. The Start menu page has a checkbox to create no Start menu folder.

## Command line switches

| Switch | Effect |
| --- | --- |
| `/SILENT` | Progress window only, no questions |
| `/VERYSILENT` | No windows at all |
| `/CURRENTUSER` | Install for the current user |
| `/ALLUSERS` | Install for all users (admin rights needed) |
| `/DIR="<folder>"` | Target folder |
| `/PORTABLE=1` | Portable installation (set `/DIR` as well for a silent run) |
| `/TASKS="desktopicon"` | Create the desktop shortcut in a silent run |
| `/NOICONS` | Create no Start menu folder |

Uninstall: run `unins000.exe /VERYSILENT` from the install folder. A silent uninstall keeps settings and logs. The interactive uninstaller asks whether to delete them too.

An installed version that is newer than the setup blocks the setup. The same or an older version is simply installed over: the running app is closed, settings are kept, and the folder and Start menu pages are skipped.

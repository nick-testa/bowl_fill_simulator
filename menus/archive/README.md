# Archived menus

Menu mappings set aside from `menus/`. The app loads only the files directly in
`menus/`, never subfolders, so nothing here appears in the app or reaches lab37's
compiled curves.

## 2026-10-14/: the menus before the 2026-10-26 refresh

Every menu as it stood when `menus/` last changed (2026-10-14):

- the previous versions of the five active brands (BIG JERK, Casa Condesa, M+R2, ORO,
  The Hungry Cowgirl), replaced in `menus/` by each brand's current store mapping at
  451 Melwood Ave, Pittsburgh, pulled on 2026-10-26;
- the four deprecated brands (Farmstand, Meat + Rice, Pita Dust, The Hungry Siren),
  which are no longer served.

`tests/verify.cc` loads this snapshot as a frozen fixture: most of its checks pin
details of particular menus (recipes, portions, counts), and a fixed copy keeps them
from breaking every time the current menus are pulled. The current menus in `menus/`
are checked on their own, in the checks' last section.

**To pull the current menus again**, read each store's mapping with lab37's bridge tool
(`lab37-bridge-service-tool --store-id <id> --get-menu-mappings`) and save its output
unchanged as `menus/<Brand>.json`. Stores of the same brand can carry different
portions, so take the same store each time.

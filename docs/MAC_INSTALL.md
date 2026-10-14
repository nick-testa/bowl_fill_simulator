# Installing the Bowl Fill Simulator on a Mac

You'll get a file called **BowlFillSimulator-mac.zip**. You don't need to install anything else.

It needs **macOS 26 (Tahoe) or newer**. To check, open the Apple menu → **About This Mac**.
If it shows an older version, update first in **System Settings → General → Software Update**.

## The first time

1. **Unzip it.** Double-click `BowlFillSimulator-mac.zip` in your Downloads folder.
   An app called **Bowl Fill Simulator** appears next to it.
2. **Move it to Applications.** Open Finder, then drag **Bowl Fill Simulator**
   onto **Applications** in the sidebar.
3. **Open it.** Double-click it in Applications. The first time, your Mac will say it
   *"cannot be opened"* or *"cannot verify"* it. That's expected: the app is built
   in-house rather than downloaded from the App Store. Click **Done** (or **OK**), **not**
   *Move to Trash*.
4. **Allow it once.** Open **System Settings** (Apple menu ⌘ top-left) →
   **Privacy & Security**. Scroll down to the message about *"Bowl Fill Simulator"*
   and click **Open Anyway**. Enter your Mac password if asked, then click
   **Open Anyway** again.

From then on it opens with a normal double-click. You can keep it in the Dock by
right-clicking its icon while it's open → **Options** → **Keep in Dock**.

## Getting a new version

When you're sent a new zip, do steps 1–4 again. When asked, choose **Replace**.

Your uploaded measurements and saved scenarios are kept. The menus update to the
new version.

## Where your files live

The app keeps its menus and measurements in a folder of its own. To find it, open
Finder, press **⌘ Shift G**, paste in the path below and press Return:

    ~/Library/Application Support/Lab37/Bowl Fill Simulator

If an update replaced an ingredient list you had edited, your old copy is in the
`backups` folder there.

Reports you export (the CSV files) save wherever you choose in the save window,
just like any other app.

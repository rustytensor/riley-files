# Riley Files: plan

Riley Files is the file manager of the Riley desktop: quiet and pleasant to look at, quick from the
keyboard, with menus and an options panel for people who like to set things up their way.

## Principles

- **Nothing in the background.** Folders are read when they are opened; thumbnails are made when they are
  shown; search runs only while you search. No indexer, no daemon.
- **Never lose a file.** Delete means trash, every file operation can be undone, a name clash is asked
  about and never overwritten, a long copy can be stopped.
- **Share with the desktop.** Thumbnails in `~/.cache/thumbnails`, default apps in `mimeapps.list`, custom
  icons as GIO metadata, drives and network places through GIO.
- **Modular.** A core library with no interface (listing, file operations, thumbnails, search, apps,
  snapshots), the QML interface on top, plugins for extras.
- **Every decision in C++.** QML lays things out and animates them; behaviour lives in C++ and is tested
  there.

## Milestones

Each ends at a gate: one command, real inputs, expected results written before the code, shown to fail
on a planted fault.

0. Skeleton: build, modules, the first core test, the window loads, the package builds.
1. Browsing: listing off the UI thread, watching, sorting, hidden files; list, grid and columns; the tree;
   keyboard.
2. File operations: copy, move, rename, new folder, trash, undo, clashes, progress and stop.
3. Sidebar, details pane, thumbnails and previews.
4. Menus, options panel, pane widths, custom icons, default apps.
5. Search by name and inside files.
6. Drives, network places, trash view, recent files.
7. Previous versions from Btrfs snapshots.
8. Look and polish, translations, accessibility.
9. 3D graph view.

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "drives.h"
#include "filesearch.h"
#include "foldermodel.h"
#include "graphbuilder.h"
#include "networkplaces.h"
#include "pinnedfolders.h"
#include "places.h"
#include "recentfiles.h"
#include "trashmodel.h"
#include "versionmodel.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes the core's types known to QML by name. The core itself stays free of QML.
struct FolderModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::FolderModel)
    QML_NAMED_ELEMENT(FolderModel)
    QML_UNCREATABLE("Folder models are made by the program.")
};

struct PlacesModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::PlacesModel)
    QML_NAMED_ELEMENT(PlacesModel)
    QML_UNCREATABLE("The places are made by the program.")
};

struct PinnedModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::PinnedModel)
    QML_NAMED_ELEMENT(PinnedModel)
    QML_UNCREATABLE("The pinned folders are made by the program.")
};

struct FileSearchForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::FileSearch)
    QML_NAMED_ELEMENT(FileSearch)
    QML_UNCREATABLE("The search is made by the program.")
};

struct DrivesModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::DrivesModel)
    QML_NAMED_ELEMENT(DrivesModel)
    QML_UNCREATABLE("The drives are made by the program.")
};

struct NetworkModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::NetworkModel)
    QML_NAMED_ELEMENT(NetworkModel)
    QML_UNCREATABLE("The network places are made by the program.")
};

struct TrashModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::TrashModel)
    QML_NAMED_ELEMENT(TrashModel)
    QML_UNCREATABLE("The trash view's rows are made by the program.")
};

struct RecentFilesForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::RecentFiles)
    QML_NAMED_ELEMENT(RecentFiles)
    QML_UNCREATABLE("The recent files are made by the program.")
};

struct VersionModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::VersionModel)
    QML_NAMED_ELEMENT(VersionModel)
    QML_UNCREATABLE("The previous versions are made by the program.")
};

struct GraphBuilderForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::GraphBuilder)
    QML_NAMED_ELEMENT(GraphBuilder)
    QML_UNCREATABLE("The graph is built by the program.")
};

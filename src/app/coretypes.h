// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filesearch.h"
#include "foldermodel.h"
#include "pinnedfolders.h"
#include "places.h"

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

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "foldermodel.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes the core's types known to QML by name. The core itself stays free of QML.
struct FolderModelForeign
{
    Q_GADGET
    QML_FOREIGN(Riley::FolderModel)
    QML_NAMED_ELEMENT(FolderModel)
    QML_UNCREATABLE("Folder models are made by the program.")
};

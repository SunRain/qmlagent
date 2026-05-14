// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    property int baseX: 70

    Rectangle {
        id: bindingTarget
        x: root.baseX + 3
        y: 20
        width: 40
        height: 24
        color: "#6f42c1"
    }
}

// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    property int baseValue: 40
    property int extraValue: 5

    Rectangle {
        id: multilineTarget
        property int computedValue: root.baseValue
                                    + root.extraValue
                                    + 2

        x: computedValue
        y: 20
        width: 40
        height: 24
        color: "#8250df"
    }
}

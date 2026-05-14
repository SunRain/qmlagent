// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    Rectangle {
        id: badArithmeticTarget
        objectName: "fixture.bindingBadArithmeticTarget"
        x: 20
        y: root.height + 80
        width: 72
        height: 24
        color: "#cf222e"
    }
}

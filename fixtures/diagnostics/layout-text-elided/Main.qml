// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    Text {
        objectName: "fixture.target"
        width: 48
        height: 24
        text: "A very long status message that cannot fit"
        elide: Text.ElideRight
        color: "#202124"
    }
}

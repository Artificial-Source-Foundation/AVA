import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    visible: true
    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 640
    title: "AVA Desktop"
    color: "#070A12"

    property color panel: "#0D1322"
    property color panelRaised: "#111A2D"
    property color panelHot: "#182642"
    property color line: "#26344F"
    property color textStrong: "#F4F7FB"
    property color textSoft: "#B8C2D6"
    property color textMuted: "#74839C"
    property color accent: "#8CB7FF"
    property color accentHot: "#B6D0FF"
    property color danger: "#FF8B8B"

    function sendComposer() {
        const draft = composer.text.trim()
        if (draft.length === 0)
            return

        avaDesktop.sendMessage(draft)
        composer.clear()
    }

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: avaDesktop.showCommandPalette()
    }

    Shortcut {
        sequence: "Escape"
        enabled: avaDesktop.commandPaletteVisible
        onActivated: avaDesktop.hideCommandPalette()
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#10192A" }
            GradientStop { position: 0.42; color: "#070A12" }
            GradientStop { position: 1.0; color: "#05070C" }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 292
            radius: 30
            color: root.panel
            border.color: "#1D2941"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 18

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 46
                        radius: 16
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#A5C7FF" }
                            GradientStop { position: 1.0; color: "#7B61FF" }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "A"
                            color: "#07101D"
                            font.pixelSize: 24
                            font.weight: Font.Black
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "AVA"
                            color: root.textStrong
                            font.pixelSize: 22
                            font.weight: Font.Bold
                        }

                        Text {
                            text: "Qt Quick desktop shell"
                            color: root.textMuted
                            font.pixelSize: 12
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    radius: 14
                    color: "#080D18"
                    border.color: "#202C45"

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        text: "Search sessions"
                        color: root.textMuted
                        font.pixelSize: 13
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 12
                        text: "Ctrl+K"
                        color: root.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Sessions"
                    color: root.textMuted
                    font.pixelSize: 12
                    font.letterSpacing: 1.2
                    font.weight: Font.Bold
                }

                ListView {
                    id: sessionList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: avaDesktop.sessions

                    delegate: Rectangle {
                        width: sessionList.width
                        height: 56
                        radius: 18
                        color: modelData === avaDesktop.activeSession ? root.panelHot : "transparent"
                        border.color: modelData === avaDesktop.activeSession ? "#36527E" : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: avaDesktop.switchSession(modelData)
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: root.textStrong
                                elide: Text.ElideRight
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData === avaDesktop.activeSession ? "Active workspace" : "Prototype session"
                                color: root.textMuted
                                elide: Text.ElideRight
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    radius: 22
                    color: "#091827"
                    border.color: "#193452"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 4

                        Text {
                            text: "Desktop path"
                            color: root.accentHot
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "QML owns presentation. C++ keeps sessions, permissions, tools, and providers."
                            color: root.textSoft
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 34
            color: "#0A0F1C"
            border.color: "#1E2A44"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: avaDesktop.activeSession
                            color: root.textStrong
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }

                        Text {
                            text: "Experimental Qt Quick/QML desktop prototype"
                            color: root.textMuted
                            font.pixelSize: 14
                        }
                    }

                    Button {
                        text: "Command Palette"
                        onClicked: avaDesktop.showCommandPalette()

                        contentItem: Text {
                            text: parent.text
                            color: root.textStrong
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        background: Rectangle {
                            radius: 16
                            color: parent.hovered ? root.panelHot : root.panelRaised
                            border.color: root.line
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: avaDesktop.permissionVisible ? 94 : 0
                    visible: avaDesktop.permissionVisible
                    radius: 24
                    color: "#211A11"
                    border.color: "#6F4A22"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            radius: 14
                            color: "#3C2A16"

                            Text {
                                anchors.centerIn: parent
                                text: "sh"
                                color: "#FFD6A3"
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                text: "Permission request"
                                color: "#FFE1B8"
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Prototype card for approving file and shell actions before they reach AVA's backend policy layer."
                                color: "#D4B990"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                            }
                        }

                        Button {
                            text: "Deny"
                            onClicked: avaDesktop.denyPermission()

                            contentItem: Text {
                                text: parent.text
                                color: root.danger
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }

                            background: Rectangle {
                                radius: 14
                                color: parent.hovered ? "#3A1D22" : "#241419"
                                border.color: "#6B303A"
                            }
                        }

                        Button {
                            text: "Allow"
                            onClicked: avaDesktop.approvePermission()

                            contentItem: Text {
                                text: parent.text
                                color: "#07101D"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }

                            background: Rectangle {
                                radius: 14
                                color: parent.hovered ? "#FFD8A8" : "#FFC36E"
                            }
                        }
                    }
                }

                ScrollView {
                    id: transcriptScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Column {
                        id: transcriptColumn

                        width: transcriptScroll.availableWidth
                        spacing: 14
                        padding: 4

                        Repeater {
                            model: avaDesktop.transcript

                            delegate: Item {
                                width: transcriptColumn.width
                                height: bubble.height

                                readonly property bool fromUser: modelData.indexOf("You:") === 0
                                readonly property bool systemLine: modelData.indexOf("System:") === 0

                                Rectangle {
                                    id: bubble

                                    width: systemLine ? parent.width : Math.min(parent.width * 0.78, 760)
                                    height: messageText.implicitHeight + 30
                                    anchors.right: fromUser ? parent.right : undefined
                                    anchors.left: fromUser ? undefined : parent.left
                                    radius: 24
                                    color: systemLine ? "#0B1322" : fromUser ? "#17345C" : root.panelRaised
                                    border.color: systemLine ? "#24314B" : fromUser ? "#3D6AA3" : "#263653"

                                    Text {
                                        id: messageText

                                        anchors.fill: parent
                                        anchors.margins: 15
                                        text: modelData
                                        color: systemLine ? root.textMuted : root.textStrong
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 14
                                        lineHeight: 1.18
                                    }
                                }
                            }
                        }

                        Item {
                            width: transcriptColumn.width
                            height: streamBubble.visible ? streamBubble.height : 0

                            Rectangle {
                                id: streamBubble

                                visible: avaDesktop.streamingText.length > 0
                                width: Math.min(parent.width * 0.78, 760)
                                height: streamingMessage.implicitHeight + 34
                                radius: 24
                                color: "#101B31"
                                border.color: "#36527E"

                                Text {
                                    id: streamingMessage

                                    anchors.fill: parent
                                    anchors.margins: 16
                                    text: "AVA: " + avaDesktop.streamingText
                                    color: root.textStrong
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 14
                                    lineHeight: 1.18
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 124
                    radius: 28
                    color: "#0D1424"
                    border.color: "#283958"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        TextArea {
                            id: composer

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: "Ask AVA to inspect, change, or explain something..."
                            color: root.textStrong
                            placeholderTextColor: root.textMuted
                            selectionColor: "#2F69C7"
                            selectedTextColor: "#FFFFFF"
                            wrapMode: TextEdit.Wrap
                            font.pixelSize: 15

                            background: Rectangle {
                                color: "transparent"
                            }

                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Return && (event.modifiers & Qt.ControlModifier)) {
                                    root.sendComposer()
                                    event.accepted = true
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: "Ctrl+Enter sends. Ctrl+K opens commands. Backend integration comes next."
                                color: root.textMuted
                                font.pixelSize: 12
                            }

                            Button {
                                text: "Send"
                                onClicked: root.sendComposer()

                                contentItem: Text {
                                    text: parent.text
                                    color: "#06101D"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 14
                                    font.weight: Font.Black
                                }

                                background: Rectangle {
                                    implicitWidth: 96
                                    implicitHeight: 42
                                    radius: 16
                                    color: parent.hovered ? root.accentHot : root.accent
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: avaDesktop.commandPaletteVisible
        z: 100
        color: "#9904070D"

        MouseArea {
            anchors.fill: parent
            onClicked: avaDesktop.hideCommandPalette()
        }

        Rectangle {
            width: Math.min(parent.width - 80, 760)
            height: 390
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 86
            radius: 28
            color: "#0C1322"
            border.color: "#344767"

            MouseArea {
                anchors.fill: parent
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: "Command Palette"
                    color: root.textStrong
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    placeholderText: "Type a command or action"
                    color: root.textStrong
                    placeholderTextColor: root.textMuted
                    focus: avaDesktop.commandPaletteVisible
                    font.pixelSize: 15

                    background: Rectangle {
                        radius: 16
                        color: "#080D18"
                        border.color: "#263957"
                    }
                }

                ListModel {
                    id: commandModel

                    ListElement { name: "Connect provider"; detail: "Open the login and provider setup flow" }
                    ListElement { name: "Switch model"; detail: "Choose a configured model profile" }
                    ListElement { name: "Review permissions"; detail: "Inspect active grants and rules" }
                    ListElement { name: "Open session"; detail: "Resume or branch a previous workspace" }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: commandModel

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 58
                        radius: 16
                        color: "#101B31"
                        border.color: "#223451"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: name
                                color: root.textStrong
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: detail
                                color: root.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}

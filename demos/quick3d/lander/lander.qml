/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
** $QT_END_LICENSE$
**
****************************************************************************/

import Qt 4.7
import Qt3D 1.0
import Qt3D.Shapes 1.0

Rectangle {
    id: screen
    width: 1000
    height: 800
    color: "black"

    // Joystix font available for free from Ray Larabie via
    // http://typodermicfonts.com/the-larabie-fonts-collection
    // Licence does *NOT* allow distribution in ttf form as part of the
    // source package.
    FontLoader { source: "JOYSTIX.TTF" }
    // Normally, you'd give the font an id and use fontId.name
    // we hard-wire it here in order to get a silent failure if
    // the font is not present.
    property string fontString: "Joystix";

    Viewport  {
        id: viewport
        y: 30
        anchors.fill: parent
        visible: false

        Rectangle {
            id: fuelGauge
        }

        camera: Camera {
            eye.x: cameraTarget.x
            // Keep the lander and pad in view for reasonable values
            eye.y: (Math.abs(lander.x) * 2.0) + (lander.y * 2.0) + (Math.abs(lander.z) * 2.0) + 5.0
            //            eye.y: (Math.abs(lander.x) * 2.0) + (lander.y * 2.0)  + 5.0
            eye.z: lander.z + 20.0
            //            eye.z: 20.0
            center: cameraTarget.position
        }

        Item3D {
            id:cameraTarget
            x: ((lander.x + landingPad.x) / 2.0)
            y: 0
            z: 0
        }

        Item3D {
            // Landing pad must come before lander so that transparency on
            // the flames is in the correct order
            id: landingPad
            scale: 0.005
            y: -2.4
            mesh: Mesh { source: "meshes/lintel.3ds"}

            // HACK.  There should be API for this
            property real yMax : y + 2.4;
        }

        Item3D {
            id: lander
            scale: 0.5
            mesh: Mesh { source: "meshes/lunar-lander.3ds"; options: "ForceSmooth" }
            effect: Effect {
                color: "#aaca00"
//                texture: "rusty.png"
                decal: true
            }

            transform: [
                Rotation3D {
                    Behavior on angle { NumberAnimation { duration: 200}}
                    axis: Qt.vector3d(-1.0,0,0)
                    angle: gameLogic.zBoostInput * 50
                },
                Rotation3D {
                    Behavior on angle { NumberAnimation { duration: 200}}
                    axis: Qt.vector3d(0,0,1.0)
                    angle: gameLogic.xBoostInput * 50
                }
            ]

            // HACK.  There should be API for this
            property real yMin: y - 0.97;
            property bool jetsVisible: true
            property real yBoostScaleFactor: (gameLogic.yboosting ? 1.5 : 1.0)
            // Back
            Jet { z: -2.7
                scaleFactor: lander.yBoostScaleFactor - gameLogic.zBoostInput
                enabled: lander.jetsVisible
            }
            // Left
            Jet { x: -2.7
                scaleFactor: lander.yBoostScaleFactor - gameLogic.xBoostInput
                enabled: lander.jetsVisible
            }
            // Right
            Jet { x: 2.7
                scaleFactor: lander.yBoostScaleFactor + gameLogic.xBoostInput
                enabled:  lander.jetsVisible
            }

            // Front
            Jet { z: 2.7
                scaleFactor: lander.yBoostScaleFactor + gameLogic.zBoostInput
                enabled:  lander.jetsVisible
            }

        }

        MouseArea {
            id: gameInputPad
            anchors.fill: parent
            enabled: false

            onMousePositionChanged: {
                gameLogic.yboosting = true;
                gameLogic.xBoostInput = (mouseX / viewport.width) - 0.5;
                gameLogic.zBoostInput = (mouseY / viewport.height) - 0.5;
            }
            onPressed: {
                gameLogic.yboosting = true;
                gameLogic.xBoostInput = (mouseX / viewport.width) - 0.5;
                gameLogic.zBoostInput = (mouseY / viewport.height) - 0.5;
            }
            onReleased: {
                gameLogic.yboosting = false;
                gameLogic.xBoostInput = 0.0;
                gameLogic.zBoostInput = 0.0;
            }
        }

        // TODO : Key input

        Item {
            id: gameLogic
            visible: false
            property string state: "titleScreen"

            // Game State
            property int score : 0
            property int hiScore : 0
            property real xBoostInput: 0.0
            property real xVelocity : 0
            property real xBoostFactor: gravity

            property bool yboosting: false
            property real yVelocity : 0
            property real yBoostFactor: gravity * 2.0

            property real zBoostInput: 0.0
            property real zVelocity : 0
            property real zBoostFactor: gravity

            // Constants
            property real gravity: 0.1 / 60.0;

            Timer {
                id: simulationTickTimer
                running: false
                interval: 1000.0 / 60.0
                repeat: true
                onTriggered: {
                    gameLogic.tick()
                }
            }

            function tick() {
                // apply gravity and user inputs to velocities
                yVelocity -= gravity;
                if (yboosting)
                    yVelocity += yBoostFactor;
                xVelocity -= xBoostInput * xBoostFactor;
                zVelocity -= zBoostInput * zBoostFactor;

                // update lander position
                lander.x += xVelocity;
                lander.y += yVelocity;
                lander.z += zVelocity;

                // Check win condition
                if (lander.yMin <= landingPad.yMax)
                    win();

            }

            function win() {
                // Theoretical max score is 100^5, or 10,000,000,000
                score = Math.floor(sanitize(xVelocity) / sanitize(yVelocity) /
                                   sanitize(zVelocity) / sanitize(lander.x)
                                   / sanitize(lander.z)
                                   );
                if (score > hiScore) hiScore = score;
                simulationTickTimer.running = false;
                endGame();
            }

            // When calculating scores, don't divide by zero and ignore sign.
            // Can't have infinite scores, and don't want negative ones!
            function sanitize(value) {
                return Math.max(0.01, Math.abs(value));
            }

            function newGame() {
                state = "playing";
                titleBar.visible = false;
                tapToStart.visible = false;
                viewport.visible = true;
                simulationTickTimer.running = true;
                gameInputPad.enabled = true;
                lander.jetsVisible = true;

                // reset state
                score = 0;
                xBoostInput = 0.0
                xVelocity = 0
                zBoostInput = 0.0
                zVelocity = 0
                yboosting = false

                // Random starting position
                lander.position = Qt.vector3d(Math.random() * 10.0 - 5.0, 5, Math.random() * 10.0 - 5.0);
                // add a small positive yVelocity to give the player a chance
                // to get their bearings
                yVelocity = 0.1
            }

            function endGame() {
                state = "titleScreen";
                titleBar.visible = true;
                tapToStart.visible = true;
                simulationTickTimer.running = false;
                gameInputPad.enabled = false;
                lander.jetsVisible = false;
            }
        }
    }

    Row {
        id: scoreBar
        anchors.left: parent.left
        anchors.right: parent.right
        Column {
            // Player 1 Score
            width: parent.width / 3.0
            Text {
                text: "Player 1"
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: fontString;
                color: "red"
            }
            Text {
                id: scoreBoardText
                text: "Score: " + gameLogic.score
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: fontString;
                color: "white"
            }
        }
        Column {
            // Hi Score
            width: parent.width / 3.0
            Text {
                text: "Hi Score"
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: fontString;
                color: "red"
            }
            Text {
                text: "Score: " + gameLogic.hiScore
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: fontString;
                color: "white"
            }
        }
        Column {
            // Player 2 Score (Not used)
            width: screen.width / 3.0
            Text {
                text: "Player 2"
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: fontString;
                color: "red"
            }
        }
    }

    Text {
        id: titleBar
        anchors.horizontalCenter: parent.horizontalCenter
        y: screen.height / 5.0
        text: "Qt-Lander"
        font.family: fontString
        font.pointSize: 48
        color: "white"
        SequentialAnimation on color {
            loops: Animation.Infinite
            ColorAnimation { to: "#ff0000"; duration: 100 }
            ColorAnimation { to: "#ffff00"; duration: 100 }
            ColorAnimation { to: "#00ff00"; duration: 100 }
            ColorAnimation { to: "#00ffff"; duration: 100 }
            ColorAnimation { to: "#0000ff"; duration: 100 }
            ColorAnimation { to: "#ff00ff"; duration: 100 }
        }
    }

    Item {
        id: tapToStart
        anchors.fill: parent
        Text {
            text: "Tap to Play"
            anchors.centerIn: parent
            font.family: fontString
            font.pointSize: 20
            color: "#2288ff"
        }
        MouseArea {
            // Note - this mousearea will be obscured by the game's mousearea
            // during play
            anchors.fill: parent
            onClicked: gameLogic.newGame();
        }
    }

    ShaderProgram {
        id: flame
        blending: true
        texture: "flame.png"
        property variant texture2 : "flame2.png"
        property real interpolationFactor : 1.0

        SequentialAnimation on interpolationFactor {
            running: true
            loops: Animation.Infinite
            NumberAnimation { to : 1.0; duration: 750; }


            PauseAnimation { duration: 550 }
            NumberAnimation { to : 0.0; duration: 750; }
            PauseAnimation { duration: 550 }
        }

        SequentialAnimation on color{
            running: true
            loops: Animation.Infinite
            ColorAnimation {
                from: "#aaca00"
                to: "#0033ca"
                duration: 500
            }
            ColorAnimation {
                from: "#0033ca"
                to: "#aaca00"
                duration: 500
            }
        }

        vertexShader: "
        attribute highp vec4 qt_Vertex;
        attribute highp vec4 qt_MultiTexCoord0;
        uniform mediump mat4 qt_ModelViewProjectionMatrix;
        varying highp vec4 texCoord;

        void main(void)
        {
        gl_Position = qt_ModelViewProjectionMatrix * qt_Vertex;
        texCoord = qt_MultiTexCoord0;
        }
        "
        fragmentShader: "
        varying highp vec4 texCoord;
        uniform sampler2D qt_Texture0;
        uniform sampler2D texture2;
        uniform mediump vec4 qt_Color;
        uniform mediump float interpolationFactor;

        void main(void)
        {
        mediump vec4 col1 = texture2D(qt_Texture0, texCoord.st);
        mediump vec4 col2 = texture2D(texture2, texCoord.st);
        gl_FragColor = mix(col1, col2, interpolationFactor);
        }
        "
    }
}

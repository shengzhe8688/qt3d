!include( ../examples.pri ) {
    error( "Couldn't find the examples.pri file!" )
}

QT += qml quick 3dcore 3drender 3dinput 3dquick 3dextras 3dquickextras

SOURCES += \
    main.cpp


RESOURCES += \
    bigscene-instanced-qml.qrc

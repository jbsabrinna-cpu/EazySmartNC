QT += core gui widgets concurrent svg opengl openglwidgets

CONFIG += c++17

# Eviter que Windows.h definisse min/max comme macros (MSVC)
DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN

#────────────────────────────────────────────
# SOURCES
#────────────────────────────────────────────
SOURCES += \
    cadview3d.cpp \
    decoupe3d.cpp \
    usinage3deditor.cpp \
    calculdialog.cpp \
    helpanimwidget.cpp \
    clipper2/clipper.engine.cpp \
    clipper2/clipper.offset.cpp \
    clipper2/clipper.rectclip.cpp \
    clipper2utils.cpp \
    occoffset.cpp \
    cadimporter.cpp \
    cadviewwidget.cpp \
    configdialog.cpp \
    contoureditor.cpp \
    crashhandler.cpp \
    cycles/blockgenerator.cpp \
    cycles/contourgenerator.cpp \
    cycles/pochecontourgenerator.cpp \
    cycles/pochecircgenerator.cpp \
    cycles/pocherectgenerator.cpp \
    cycles/percagegenerator.cpp \
    cycles/fraisagetrougenerator.cpp \
    cycles/usinage3dgenerator.cpp \
    blkmodele3d.cpp \
    executionpage.cpp \
    filebrowserpage.cpp \
    gcodeeditor.cpp \
    gcodeengine.cpp \
    main.cpp \
    VcpController.cpp \
    mainwindow.cpp \
    modeusin_widget.cpp \
    modules/centroid_api/centroidstatewrapper.cpp \
    modules/centroid_api/centroidapiinterface.cpp \
    modules/centroid_api/centroidbridgewrapper.cpp \
    modules/WorkCoord/WorkCoordWidget.cpp \
    modules/machinecoord/machinecoordwidget.cpp \
    modules/distancetogo/distancetogowidget.cpp \
    programmepage.cpp \
    sim3dwidget.cpp \
    tooldatabase.cpp \
    tooltabledialog.cpp \
    translator.cpp \
    uniteditor.cpp

#────────────────────────────────────────────
# HEADERS
#────────────────────────────────────────────
HEADERS += \
    cadview3d.h \
    decoupe3d.h \
    usinage3deditor.h \
    calculdialog.h \
    progression.h \
    helpanimwidget.h \
    occoffset.h \
    cadimporter.h \
    configdialog.h \
    cycles/blockgenerator.h \
    cycles/contourgenerator.h \
    cycles/pochecontourgenerator.h \
    cycles/pochecircgenerator.h \
    cycles/pocherectgenerator.h \
    cycles/percagegenerator.h \
    cycles/fraisagetrougenerator.h \
    cycles/usinage3dgenerator.h \
    blkmodele3d.h \
    filepreviewwidget.h \
    cadviewwidget.h \
    contoureditor.h \
    contourelement.h \
    crashhandler.h \
    pntfilebrowserdialog.h \
    eazyfiledialog.h \
    pointfile.h \
    pointfileeditordialog.h \
    pointspreviewwidget.h \
    sim2dviews.h \
    sim3dwidget.h \
    simulationpage.h \
    ncfileparser.h \
    tnc620theme.h \
    translator.h \
    uniteditor.h \
    VcpController.h \
    VcpEvents.h \
    executionpage.h \
    filebrowserpage.h \
    drodata.h \
    gcodeeditor.h \
    gcodeengine.h \
    mainwindow.h \
    modeusin_widget.h \
    stockalphasoftkey.h \
    modules/centroid_api/centroidstatewrapper.h \
    modules/centroid_api/centroidapiinterface.h \
    modules/centroid_api/centroidbridgewrapper.h \
    modules/WorkCoord/WorkCoordWidget.h \
    modules/machinecoord/machinecoordwidget.h \
    modules/distancetogo/distancetogowidget.h \
    programmeblock.h \
    programmepage.h \
    tooldatabase.h \
    tooltabledialog.h

#────────────────────────────────────────────
# UI FORMS
#────────────────────────────────────────────
FORMS += \
    EazyNC_MainWindow_v4.ui \
    FileBrowserPage.ui \
    UnitEditor.ui \
    dialog.ui

#────────────────────────────────────────────
# OPTION CENTROID CNC12
#────────────────────────────────────────────
# Desactive par defaut : EazyNC compile et tourne sans CNC12 (mode
# simulation, les wrappers Centroid passent en version vide).
# Activer sur le PC machine : Projects > Build > qmake >
#   Additional arguments : CONFIG+=WITH_CENTROID
# Ou plus simple : un fichier « avec_centroid.txt » dans le dossier du projet
# (le supprimer pour revenir au mode simulation).
exists($$PWD/avec_centroid.txt): CONFIG += WITH_CENTROID
contains(CONFIG, WITH_CENTROID) {
    DEFINES += WITH_CENTROID_API
    message("*** EazyNC : compilation AVEC l'API Centroid CNC12 ***")
} else {
    message("*** EazyNC : compilation SANS CNC12 (mode simulation) ***")
}

#────────────────────────────────────────────
# INCLUDE PATHS
#────────────────────────────────────────────
INCLUDEPATH += modules/centroid_api
INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/clipper2
INCLUDEPATH += $$PWD/cycles

#────────────────────────────────────────────
# LIBS (Windows MSVC)
#────────────────────────────────────────────
LIBS += -lUser32
LIBS += -lopengl32 -lglu32

# Debug OPTIMISÉ : les calculs 3D (finition, simulation) sont 10 à 30 fois
# plus lents sans optimisation. On garde le même dossier (tools.json, DLL)
# qu'avant : pas de second tableau d'outils comme avec un kit Release.
msvc:CONFIG(debug, debug|release) {
    QMAKE_CFLAGS_DEBUG   += -O2
    QMAKE_CXXFLAGS_DEBUG += -O2
}

# dbghelp pour crashhandler (MSVC — lie automatiquement via #pragma comment)
msvc {
    LIBS += -ldbghelp
}

# CentroidWrapper uniquement en version integree
contains(DEFINES, WITH_CENTROID_API) {
    LIBS += -L$$PWD/ -lCentroidWrapper
}

#────────────────────────────────────────────
# RESOURCES
#────────────────────────────────────────────
RESOURCES += \
    icons.qrc \
    help_anim.qrc \
    aide3d.qrc

# Ressources volumineuses (animations GIF) : compilées à part par rcc
CONFIG += resources_big

#────────────────────────────────────────────
# FICHIER DE TRADUCTION (.msg style Centroid CNC12)
#────────────────────────────────────────────
DISTFILES += \
    lang/eazync.msg \
    TNC640Global.ttf

# IMPORTANT : copier manuellement le dossier 'lang' (avec eazync.msg)
# a cote de l'executable EazyNC.exe, c.-a-d. dans :
#   build/.../debug/lang/eazync.msg
# Le Translator cherche aussi dans le repertoire courant, donc
# avoir 'lang/eazync.msg' a cote du .pro fonctionne en general
# quand on lance depuis Qt Creator.

#────────────────────────────────────────────
# OpenCASCADE 7.9.3 (optionnel)
# Activer : Projects > Build > qmake > Additional arguments : CONFIG+=WITH_OCC
#────────────────────────────────────────────
contains(CONFIG, WITH_OCC) {
    message("*** EazyNC : compilation WITH_OCC (OpenCASCADE 8.0.1 via vcpkg) ***")

    VCPKG_ROOT = "C:/vcpkg-master/vcpkg-master/installed/x64-windows"
    OCC_INC = $$VCPKG_ROOT/include/opencascade
    OCC_LIB = $$VCPKG_ROOT/lib

    INCLUDEPATH += $$OCC_INC
    LIBS += -L$$OCC_LIB
    LIBS += -lTKernel \
            -lTKMath \
            -lTKG3d \
            -lTKG2d \
            -lTKGeomBase \
            -lTKGeomAlgo \
            -lTKBRep \
            -lTKTopAlgo \
            -lTKShHealing \
            -lTKPrim \
            -lTKBO \
            -lTKBool \
            -lTKOffset \
            -lTKFillet \
            -lTKMesh \
            -lTKXSBase \
            -lTKDE \
            -lTKDESTEP \
            -lTKDEIGES \
            -lTKDECascade

    DEFINES += WITH_OCC
    QMAKE_CXXFLAGS += /wd4100 /wd4127 /wd4189 /wd4996

    message("  Headers : $$OCC_INC")
    message("  Libs    : $$OCC_LIB")

    # Copie des DLL OpenCASCADE (et dépendances) à côté de l'exe après
    # chaque édition de liens (/D : seulement si plus récentes)
    OCC_BIN = $$shell_path($$VCPKG_ROOT/bin)
    EXE_DIR = $$shell_path($$OUT_PWD/debug)
    CONFIG(release, debug|release): EXE_DIR = $$shell_path($$OUT_PWD/release)
    QMAKE_POST_LINK += xcopy /D /Y /Q \"$$OCC_BIN\\*.dll\" \"$$EXE_DIR\\\" $$escape_expand(\\n\\t)
    # API Centroid : toujours la DLL livrée avec le CNC12 installé (une DLL
    # d'une autre version décale les codes retour, ex. SUCCESS 27 -> 30)
    contains(DEFINES, WITH_CENTROID_API): exists(C:/cncm/CentroidAPI.dll) {
        QMAKE_POST_LINK += copy /Y \"C:\\cncm\\CentroidAPI.dll\" \"$$EXE_DIR\\CentroidAPI.dll\" $$escape_expand(\\n\\t)
    }
}

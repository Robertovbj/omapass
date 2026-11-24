QT += core gui testlib network concurrent
CONFIG += testcase c++20 link_pkgconfig
# Botan 3 where the distribution has it (Arch), Botan 2 where it does not
# (Ubuntu 24.04 LTS ships 2.19). Everything omapass asks of Botan exists in
# both; the two calls whose spelling changed are handled in bwcrypto.cpp.
packagesExist(botan-3) {
    PKGCONFIG += botan-3
} else {
    PKGCONFIG += botan-2
}
PKGCONFIG += zlib
DEFINES += OMAPASS_SRC_ROOT=\\\"$$PWD/..\\\"
DEFINES += OMAPASS_BW_FIXTURES=\\\"$$PWD/fixtures/bitwarden\\\"
DEFINES += OMAPASS_OP_FIXTURES=\\\"$$PWD/fixtures/onepassword\\\"
DEFINES += OMAPASS_FAKE_CLIS=\\\"$$PWD/fakes\\\"
TEMPLATE = app
TARGET = tst_omapass

INCLUDEPATH += ../src

SOURCES += \
    main.cpp \
    tst_bitwarden.cpp \
    tst_browser.cpp \
    tst_browsercrypto.cpp \
    tst_clipboard.cpp \
    tst_config.cpp \
    tst_entryicons.cpp \
    tst_filter.cpp \
    tst_generator.cpp \
    tst_history.cpp \
    tst_iconchoice.cpp \
    tst_kdbx.cpp \
    tst_onepassword.cpp \
    tst_pass.cpp \
    tst_pin.cpp \
    tst_secret.cpp \
    tst_hyprrounding.cpp \
    tst_sessionlock.cpp \
    tst_siteicons.cpp \
    tst_sshagent.cpp \
    tst_sshaskpass.cpp \
    tst_totp.cpp \
    tst_updater.cpp \
    ../src/appcontroller.cpp \
    ../src/bitwardenlogin.cpp \
    ../src/entryrowsmodel.cpp \
    ../src/bitwardenjson.cpp \
    ../src/bitwardenvault.cpp \
    ../src/bwcrypto.cpp \
    ../src/bwaccount.cpp \
    ../src/bwapi.cpp \
    ../src/browsercrypto.cpp \
    ../src/browserintegration.cpp \
    ../src/browsermanifests.cpp \
    ../src/browserprotocol.cpp \
    ../src/browserproxy.cpp \
    ../src/browserserver.cpp \
    ../src/browserstore.cpp \
    ../src/clipboard.cpp \
    ../src/config.cpp \
    ../src/entryicons.cpp \
    ../src/filter.cpp \
    ../src/generator.cpp \
    ../src/history.cpp \
    ../src/i18n.cpp \
    ../src/iconchoice.cpp \
    ../src/kdbx.cpp \
    ../src/kdbx2pass.cpp \
    ../src/keepassvault.cpp \
    ../src/onepasswordlogin.cpp \
    ../src/onepasswordvault.cpp \
    ../src/opjson.cpp \
    ../src/passstore.cpp \
    ../src/passvault.cpp \
    ../src/pin.cpp \
    ../src/process.cpp \
    ../src/secret.cpp \
    ../src/hyprrounding.cpp \
    ../src/sessionlock.cpp \
    ../src/siteicons.cpp \
    ../src/sshagent.cpp \
    ../src/sshaskpass.cpp \
    ../src/totp.cpp \
    ../src/updater.cpp \
    ../src/vault.cpp

HEADERS += \
    support.h \
    ../src/appcontroller.h \
    ../src/bitwardenlogin.h \
    ../src/entryrowsmodel.h \
    ../src/bitwardenjson.h \
    ../src/bitwardenvault.h \
    ../src/bwaccount.h \
    ../src/bwapi.h \
    ../src/browsercrypto.h \
    ../src/browserintegration.h \
    ../src/browsermanifests.h \
    ../src/browserprotocol.h \
    ../src/browserproxy.h \
    ../src/browserserver.h \
    ../src/browserstore.h \
    ../src/clipboard.h \
    ../src/bwcrypto.h \
    ../src/config.h \
    ../src/entryicons.h \
    ../src/filter.h \
    ../src/generator.h \
    ../src/history.h \
    ../src/i18n.h \
    ../src/iconchoice.h \
    ../src/kdbx.h \
    ../src/kdbx2pass.h \
    ../src/keepassvault.h \
    ../src/onepasswordlogin.h \
    ../src/onepasswordvault.h \
    ../src/opjson.h \
    ../src/passstore.h \
    ../src/passvault.h \
    ../src/pin.h \
    ../src/process.h \
    ../src/secret.h \
    ../src/hyprrounding.h \
    ../src/sessionlock.h \
    ../src/siteicons.h \
    ../src/sshagent.h \
    ../src/sshaskpass.h \
    ../src/totp.h \
    ../src/updater.h \
    ../src/vault.h

RESOURCES += ../src/resources.qrc

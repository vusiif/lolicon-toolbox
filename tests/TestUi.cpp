#include <QtTest>
#include <ElaApplication.h>
#include <ElaTheme.h>
#include <ElaAppBar.h>
#include <ElaToolButton.h>
#include <ElaNavigationBar.h>
#include <ElaComboBox.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaLineEdit.h>
#include <ElaIconButton.h>
#include "TestHttpServer.h"
#include <QTemporaryDir>
#include <QLabel>
#include <QClipboard>
#include <QStyleHints>
#include <QSettings>
#include <QLocale>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QTreeView>
#include "MainWindow.h"
#include "LazyToolPage.h"
#include "SettingsPage.h"
#include "UiTheme.h"
#include "Base64Page.h"
#include "MojibakePage.h"
#include "TextFileRecoveryPage.h"
#include "ByteEncodingAnalyzer.h"
#include "FilenameRecoveryPage.h"
#include "ClipboardSourcePage.h"
#include "DiagnosticReportWidget.h"
#include "TextStylePage.h"
#include "VoiceDesignerPage.h"
#include "CharacterArtPage.h"
#include <ElaSpinBox.h>
#include <QSemaphore>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMimeData>
#include <QTextDocument>
#include <QStackedWidget>
#include <QDir>
#include <QFile>
#include "services/AppSettings.h"
#include "tool/ToolRegistry.h"
#include "navigation/NavigationManager.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class TestUi : public QObject
{
    Q_OBJECT
private slots:
    void systemDefaults();
    void settingsPersistence();
    void groupedNavigation();
    void mojibakeWorkflow();
    void aiWorkflow();
    void modelListingWorkflow();
    void fileRecoveryWorkflow();
    void filenameRecoveryWorkflow();
    void clipboardSourceWorkflow();
    void diagnosticReportWorkflow();
    void textStyleWorkflow();
    void textStyleRestoreAiWorkflow();
    void voiceDesignerWorkflow();
    void characterArtWorkflow();
    void windowWorkflow();
};
void TestUi::systemDefaults()
{
    QCOMPARE(AppSettings::localeForUiLanguages({"zh-Hans-CN"}), QString("zh_CN"));
    QCOMPARE(AppSettings::localeForUiLanguages({"zh-Hant-TW"}), QString("zh_TW"));
    QCOMPARE(AppSettings::localeForUiLanguages({"zh-HK"}), QString("zh_TW"));
    QCOMPARE(AppSettings::localeForUiLanguages({"zh-SG"}), QString("zh_CN"));
    QCOMPARE(AppSettings::localeForUiLanguages({"fr-FR", "zh-Hant-HK", "en-US"}), QString("zh_TW"));
    QCOMPARE(AppSettings::localeForUiLanguages({"en-US", "zh-CN"}), QString("en"));
    QCOMPARE(AppSettings::localeForUiLanguages({"ja-JP"}), QString("en"));
    QTemporaryDir dir;
    const auto file = dir.filePath("automatic.ini");
    auto* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Light);
    QCoreApplication::processEvents();
    AppSettings settings(file);
    QVERIFY(settings.followsSystemTheme());
    QCOMPARE(settings.themeMode(), ElaThemeType::Light);
    QCOMPARE(settings.localePreference(), QString("system"));
    QCOMPARE(settings.locale(), AppSettings::localeForUiLanguages(QLocale::system().uiLanguages()));
    SettingsPage page(settings);
    QVERIFY(!page.findChild<ElaComboBox*>("themeCombo"));
    auto* language = page.findChild<ElaComboBox*>("languageCombo");
    QCOMPARE(language->currentData().toString(), QString("system"));
    ToolRegistry tools;
    NavigationManager navigationConfig;
    MainWindow window(settings, tools, navigationConfig);
    eTheme->setThemeMode(settings.themeMode());
    hints->setColorScheme(Qt::ColorScheme::Dark);
    QCoreApplication::processEvents();
    QCOMPARE(settings.themeMode(), ElaThemeType::Dark);
    QCOMPARE(eTheme->getThemeMode(), ElaThemeType::Dark);
    QVERIFY(settings.followsSystemTheme());
    settings.sync();
    QSettings saved(file, QSettings::IniFormat);
    QVERIFY(!saved.contains("Appearance/theme"));
    QVERIFY(!saved.contains("Appearance/locale"));
    settings.setThemeMode(ElaThemeType::Light);
    QVERIFY(!settings.followsSystemTheme());
    hints->setColorScheme(Qt::ColorScheme::Light);
    hints->setColorScheme(Qt::ColorScheme::Dark);
    QCOMPARE(settings.themeMode(), ElaThemeType::Light);
    QVERIFY(settings.setLocale("en"));
    QCOMPARE(language->currentData().toString(), QString("en"));
    language->setCurrentIndex(language->findData("system"));
    QCOMPARE(settings.localePreference(), QString("system"));
    QCOMPARE(settings.locale(), AppSettings::localeForUiLanguages(QLocale::system().uiLanguages()));
    hints->unsetColorScheme();
}
void TestUi::settingsPersistence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto file = dir.filePath("settings.ini");
    {
        AppSettings settings(file);
        settings.setThemeMode(ElaThemeType::Light);
        QVERIFY(settings.setLocale("zh_TW"));
        QVERIFY(!settings.setLocale("invalid"));
        QCOMPARE(settings.locale(), QString("zh_TW"));
        settings.setLastPage("settings");
        settings.setNavigationExpanded(false);
        settings.setWindowGeometry("geometry");
        settings.sync();
    }
    AppSettings restored(file);
    QCOMPARE(restored.themeMode(), ElaThemeType::Light);
    QCOMPARE(restored.locale(), QString("zh_TW"));
    QCOMPARE(restored.lastPage(), QString("settings"));
    QVERIFY(!restored.navigationExpanded());
    QCOMPARE(restored.windowGeometry(), QByteArray("geometry"));
    QVERIFY(restored.setLocale("en"));
}
void TestUi::groupedNavigation()
{
    ToolRegistry tools;
    int loads = 0;
    for (const auto& id : {"a", "b", "c", "hidden"}) {
        ToolDescriptor tool;
        tool.id = id;
        tool.name = id;
        tool.factory = [&loads](QWidget* parent) { ++loads; return new Base64Page(parent); };
        QVERIFY(tools.registerTool(tool));
    }
    NavigationManager config;
    QVERIFY(config.addGroup({"later", "Later", 20}));
    QVERIFY(config.addGroup({"first", "First", 10}));
    QVERIFY(config.addGroup({"empty", "Empty", 0}));
    QVERIFY(!config.addGroup({"first", "Duplicate", 0}));
    QVERIFY(!config.addGroup({"", "Invalid", 0}));
    QVERIFY(!config.addItem({"a", "missing", 0}));
    QVERIFY(config.addItem({"a", "later", 0}));
    QVERIFY(!config.addItem({"a", "first", 0}));
    QVERIFY(config.addItem({"b", "first", 20}));
    QVERIFY(config.addItem({"c", "first", 10}));
    QVERIFY(config.addItem({"hidden", "first", 0, false}));
    QVERIFY(config.addItem({"unregistered", "empty", 0}));
    const auto sections = config.sections(tools);
    QCOMPARE(sections.size(), 2);
    QCOMPARE(sections[0].group.id, QString("first"));
    QCOMPARE(sections[0].items.size(), 2);
    QCOMPARE(sections[0].items[0].toolId, QString("c"));
    QCOMPARE(sections[0].items[1].toolId, QString("b"));
    QCOMPARE(sections[1].group.id, QString("later"));
    QCOMPARE(sections[1].items[0].toolId, QString("a"));
    // Equal priorities have deterministic ID ordering, independent of insertion.
    NavigationManager ties;
    QVERIFY(ties.addGroup({"z", "Z", 0}));
    QVERIFY(ties.addGroup({"a", "A", 0}));
    QVERIFY(ties.addItem({"c", "a", 0}));
    QVERIFY(ties.addItem({"b", "a", 0}));
    QVERIFY(ties.addItem({"a", "z", 0}));
    QCOMPARE(ties.sections(tools)[0].group.id, QString("a"));
    QCOMPARE(ties.sections(tools)[0].items[0].toolId, QString("b"));
    QTemporaryDir dir;
    AppSettings settings(dir.filePath("groups.ini"));
    settings.setLastPage("hidden");
    MainWindow window(settings, tools, config);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    QVERIFY(window.pageKey("hidden").isEmpty());
    QVERIFY(window.groupKey("empty").isEmpty());
    QCOMPARE(loads, 0);
    window.show();
    QTest::qWait(100);
    QCOMPARE(window.getCurrentNavigationPageKey(), window.pageKey("c"));
    QCOMPARE(settings.lastPage(), QString("c"));
    QCOMPARE(loads, 1);
    window.close();
    // Close during group animations and process events in the next window.
    // Also restore compact mode without opening a group popup on startup.
    settings.setNavigationExpanded(false);
    MainWindow compact(settings, tools, config);
    compact.setAttribute(Qt::WA_DontShowOnScreen);
    compact.show();
    QTest::qWait(100);
    QVERIFY(!QApplication::activePopupWidget());
    QCOMPARE(compact.findChild<ElaNavigationBar*>()->getDisplayMode(), ElaNavigationType::Compact);
    compact.close();
}
void TestUi::mojibakeWorkflow()
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath("mojibake.ini"));
    QVERIFY(settings.setLocale("en"));
    ToolRegistry tools;
    ToolDescriptor descriptor;
    descriptor.id = "encoding.mojibake"; descriptor.name = "Mojibake Repair";
    descriptor.factory = [](QWidget* parent) { return new MojibakePage(parent); };
    QVERIFY(tools.registerTool(descriptor));
    NavigationManager config;
    QVERIFY(config.addGroup({"encoding", "Text and Encoding", 10}));
    QVERIFY(config.addItem({descriptor.id, "encoding", 20}));
    MainWindow window(settings, tools, config);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); QTest::qWait(50);
    auto* input = window.findChild<ElaPlainTextEdit*>("mojibakeInput");
    auto* output = window.findChild<ElaPlainTextEdit*>("mojibakeOutput");
    auto* process = window.findChild<ElaPlainTextEdit*>("mojibakeProcess");
    auto* analyze = window.findChild<ElaPushButton*>("mojibakeAnalyze");
    QVERIFY(input && output && process && analyze);
    input->setPlainText(QString::fromUtf8("ä½ å¥½"));
    analyze->click();
    QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好"));
    QVERIFY(process->toPlainText().contains("UTF-8"));
    QVERIFY(process->toPlainText().contains("Yes"));
    auto* first = window.findChild<ElaPushButton*>("mojibakeCandidate1");
    auto* second = window.findChild<ElaPushButton*>("mojibakeCandidate2");
    QVERIFY(first && second && first->isChecked() && second->isEnabled());
    second->click();
    QVERIFY(second->isChecked() && !first->isChecked());
    QVERIFY(output->toPlainText() != QString::fromUtf8("你好"));
    // Capture copies instead of using the system clipboard, which clipboard
    // managers and IMEs can hold open (CLIPBRD_E_CANT_OPEN).
    QString copied;
    window.findChild<MojibakePage*>()->setClipboardWriter([&copied](const QString& text) { copied = text; return true; });
    window.findChild<ElaPushButton*>("mojibakeCopy")->click();
    const auto alternative = copied;
    first->click();
    QVERIFY(first->isChecked() && !second->isChecked());
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好"));
    window.findChild<ElaPushButton*>("mojibakeCopy")->click();
    QCOMPARE(copied, QString::fromUtf8("你好"));
    QVERIFY(alternative != copied);
    QVERIFY(settings.setLocale("zh_CN"));
    QCoreApplication::processEvents();
    QCOMPARE(window.getNavigationNodeTitle(window.pageKey(descriptor.id)), QString::fromUtf8("乱码恢复"));
    QCOMPARE(analyze->text(), QString::fromUtf8("智能分析"));
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好"));
    input->setPlainText("normal text");
    QVERIFY(output->toPlainText().isEmpty());
    for (int i = 1; i <= 5; ++i) {
        auto* b = window.findChild<ElaPushButton*>(QString("mojibakeCandidate%1").arg(i));
        QVERIFY(b && !b->isEnabled() && !b->isChecked());
    }
    analyze->click();
    QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(output->toPlainText(), QString("normal text"));
    QVERIFY(window.grab().save("mojibake-normal.png"));
    input->setPlainText(QString::fromUtf8("浣犲ソ"));
    analyze->click();
    QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好"));
    window.findChild<ElaPushButton*>("mojibakeDetails")->click();
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save("mojibake-repair.png"));
    input->setPlainText(QString::fromUtf8("璇峰皬缁勮礋璐ｄ汉鍦ㄧ兢鍐呮眹鎶ュ綋鍓嶉」鐩繘搴︿互鍙婇亣鍒扮殑闂锛屾柟渚挎寚瀵艰�佸笀鎺屾彙椤圭洰鎯呭喌銆佸強鏃剁粰鍑烘寚瀵煎缓璁�傜鍚勪綅椤圭洰鎺ㄨ繘椤哄埄"));
    analyze->click();
    QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(output->toPlainText(), QString::fromUtf8("请小组负责人在群内汇报当前项目进度以及遇到的问题，方便指导�师掌握项目情况、及时给出指导建议�祝各位项目推进顺利"));
    QVERIFY(process->toPlainText().contains("丢失的字节区域以 � 标记"));
    QVERIFY(window.grab().save("mojibake-partial-repair.png"));
    input->setPlainText(QString::fromUtf8("ä½ å¥½ ").repeated(16000));
    analyze->click();
    input->setPlainText("changed during analysis");
    QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QVERIFY(output->toPlainText().isEmpty());
    window.close();
    // Destruction while a worker is running must not capture a dangling page.
    auto* page = new MojibakePage;
    page->findChild<ElaPlainTextEdit*>("mojibakeInput")->setPlainText(QString::fromUtf8("ä½ å¥½ ").repeated(16000));
    page->findChild<ElaPushButton*>("mojibakeAnalyze")->click();
    delete page;
    QTest::qWait(100);
}
void TestUi::aiWorkflow()
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath("ai-ui.ini"));
    QVERIFY(settings.setLocale("en"));
    TestHttpServer server; server.response = TestHttpServer::success(QString::fromUtf8("你好！"));
    SettingsPage config(settings);
    config.findChild<ElaLineEdit*>("aiApiUrl")->setText(server.url());
    config.findChild<ElaComboBox*>("aiApiModel")->setEditText("test-model");
    auto* key = config.findChild<ElaLineEdit*>("aiApiKey");
    QCOMPARE(key->echoMode(), QLineEdit::Password); key->setText("test-only-key");
    config.findChild<ElaPushButton*>("aiSaveSettings")->click();
    QCOMPARE(settings.aiConfiguration().model, QString("test-model"));
    QCOMPARE(server.requests, 0);
    config.resize(850, 750); config.setAttribute(Qt::WA_DontShowOnScreen); config.show();
    QVERIFY(config.grab().save("ai-settings.png"));
    MojibakePage page(nullptr, &settings);
    page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(900, 1100); page.show();
    auto* input = page.findChild<ElaPlainTextEdit*>("mojibakeInput");
    auto* local = page.findChild<ElaPlainTextEdit*>("mojibakeOutput");
    auto* output = page.findChild<ElaPlainTextEdit*>("mojibakeAiOutput");
    auto* analyze = page.findChild<ElaPushButton*>("mojibakeAnalyze");
    auto* aiAnalyze = page.findChild<ElaPushButton*>("mojibakeAiAnalyze");
    input->setPlainText(QString::fromUtf8("浣犲ソ"));
    QVERIFY(!aiAnalyze->isEnabled());
    analyze->click(); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QVERIFY(aiAnalyze->isEnabled()); QCOMPARE(server.requests, 0);
    aiAnalyze->click(); QTRY_COMPARE_WITH_TIMEOUT(output->toPlainText(), QString::fromUtf8("你好！"), 3000);
    QCOMPARE(local->toPlainText(), QString::fromUtf8("你好"));
    QString aiCopied; page.setClipboardWriter([&aiCopied](const QString& text) { aiCopied = text; return true; });
    page.findChild<ElaPushButton*>("mojibakeAiCopy")->click();
    QCOMPARE(aiCopied, QString::fromUtf8("你好！"));
    QVERIFY(page.grab().save("mojibake-ai.png"));
    QVERIFY(settings.setLocale("zh_CN"));
    QCoreApplication::processEvents(); QCOMPARE(aiAnalyze->text(), QString::fromUtf8("AI 深度分析"));
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好！"));
    server.hold = true;
    aiAnalyze->click(); QTRY_COMPARE_WITH_TIMEOUT(server.requests, 2, 3000);
    QVERIFY(page.findChild<QLabel*>("mojibakeAiStatus")->text().contains(QString::fromUtf8("已等待")));
    input->setPlainText("changed input");
    QTRY_VERIFY_WITH_TIMEOUT(!page.findChild<ElaPushButton*>("mojibakeAiCancel")->isVisible(), 3000);
    QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!aiAnalyze->isEnabled());
}
void TestUi::modelListingWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("models.ini"));
    QVERIFY(settings.setLocale("en"));
    TestHttpServer server; server.response = R"({"data":[{"id":"model-a"},{"id":"model-b"}]})";
    SettingsPage page(settings);
    auto* url = page.findChild<ElaLineEdit*>("aiApiUrl");
    auto* key = page.findChild<ElaLineEdit*>("aiApiKey");
    auto* models = page.findChild<ElaComboBox*>("aiApiModel");
    auto* fetch = page.findChild<ElaPushButton*>("aiFetchModels");
    url->setText(server.url()); key->setText("test-only-key");
    QVERIFY(models->isEditable()); QCOMPARE(server.requests, 0);
    fetch->click(); QTRY_COMPARE_WITH_TIMEOUT(models->count(), 2, 3000);
    QVERIFY(models->currentText().isEmpty());
    models->setCurrentIndex(1);
    page.findChild<ElaPushButton*>("aiSaveSettings")->click();
    QCOMPARE(settings.aiConfiguration().model, QString("model-b"));
    fetch->click(); QTRY_COMPARE_WITH_TIMEOUT(server.requests, 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(fetch->text(), QString("Fetch models"), 3000);
    QCOMPARE(models->currentText(), QString("model-b"));
    page.resize(850, 750); page.setAttribute(Qt::WA_DontShowOnScreen); page.show();
    QVERIFY(page.grab().save("ai-model-list.png"));
    key->setText("changed-test-key"); QCOMPARE(models->count(), 0);
    QCOMPARE(models->currentText(), QString("model-b"));
    server.hold = true; fetch->click(); QTRY_COMPARE_WITH_TIMEOUT(server.requests, 3, 3000);
    url->setText(server.url() + "/other");
    QTRY_COMPARE_WITH_TIMEOUT(fetch->text(), QString("Fetch models"), 3000);
    QCOMPARE(models->count(), 0);
    models->setEditText("manual-model");
    QCOMPARE(models->currentText(), QString("manual-model"));
    // Voice API: a separate form that finds the voice design model in the listing.
    auto* voiceUrl = page.findChild<ElaLineEdit*>("voiceApiUrl");
    auto* voiceKey = page.findChild<ElaLineEdit*>("voiceApiKey");
    auto* voiceModels = page.findChild<ElaComboBox*>("voiceApiModel");
    auto* voiceFetch = page.findChild<ElaPushButton*>("voiceFetchModels");
    auto* voiceSave = page.findChild<ElaPushButton*>("voiceSaveSettings");
    QVERIFY(voiceUrl && voiceKey && voiceModels && voiceFetch && voiceSave && voiceUrl != url);
    QVERIFY(!settings.hasVoiceConfiguration());
    QCOMPARE(voiceModels->currentText(), AppSettings::defaultVoiceModel()); // Never the text model.
    TestHttpServer voiceServer;
    voiceServer.response = R"({"data":[{"id":"mimo-v2.6-flash"},{"id":"mimo-v2.5-tts"},{"id":"mimo-v2.5-tts-voicedesign"}]})";
    voiceUrl->setText(voiceServer.url()); voiceKey->setText("voice-test-key");
    voiceFetch->click(); QTRY_COMPARE_WITH_TIMEOUT(voiceModels->count(), 3, 3000);
    QCOMPARE(voiceModels->itemText(0), QString("mimo-v2.5-tts-voicedesign")); // Voice design models are listed first.
    QCOMPARE(voiceModels->currentText(), QString("mimo-v2.5-tts-voicedesign"));
    voiceServer.response = R"({"data":[{"id":"other-tts"},{"id":"Acme-Voice_Design-2"}]})";
    voiceFetch->click(); QTRY_COMPARE_WITH_TIMEOUT(voiceModels->itemText(0), QString("Acme-Voice_Design-2"), 3000);
    QCOMPARE(voiceModels->currentText(), QString("Acme-Voice_Design-2")); // A model this endpoint does not offer is replaced.
    voiceServer.response = R"({"data":[{"id":"text-only"}]})";
    voiceFetch->click(); QTRY_COMPARE_WITH_TIMEOUT(voiceModels->count(), 1, 3000);
    QCOMPARE(voiceModels->currentText(), QString("Acme-Voice_Design-2")); // Nothing suitable: keep the entered model.
    voiceSave->click();
    QVERIFY(settings.hasVoiceConfiguration());
    QCOMPARE(settings.voiceConfiguration().model, QString("Acme-Voice_Design-2"));
    QCOMPARE(settings.voiceConfiguration().apiKey, QString("voice-test-key"));
    QCOMPARE(settings.aiConfiguration().model, QString("model-b")); // The text API is untouched.
    QVERIFY(page.grab().save("settings-voice-api.png"));
}
void TestUi::fileRecoveryWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("file-ui.ini")); QVERIFY(settings.setLocale("en"));
    const auto source = dir.filePath("input.log"), destination = dir.filePath("output.log");
    const QString text = "你好\r\nNBSP 😀\n";
    const auto original = mojibake::ByteEncodingAnalyzer::bom(mojibake::Encoding::Utf16LE) + *mojibake::EncodingConverter::encode(text, mojibake::Encoding::Utf16LE);
    { QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(original); }
    MojibakePage page(nullptr, &settings); page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(1000, 1000); page.show();
    auto* pasted = page.findChild<ElaPlainTextEdit*>("mojibakeInput"); pasted->setPlainText("preserve pasted input");
    page.findChild<ElaPushButton*>("mojibakeFileMode")->click();
    auto* filePage = page.findChild<TextFileRecoveryPage*>(); QVERIFY(filePage);
    auto* analyze = page.findChild<ElaPushButton*>("fileRecoveryAnalyze");
    auto* save = page.findChild<ElaPushButton*>("fileRecoverySave");
    auto* preview = page.findChild<ElaPlainTextEdit*>("fileRecoveryPreview");
    auto* details = page.findChild<ElaPlainTextEdit*>("fileRecoveryDetails");
    auto* status = page.findChild<QLabel*>("fileRecoveryStatus");
    filePage->openFile(source); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QVERIFY(preview->toPlainText().startsWith(QString::fromUtf8("你好"))); QVERIFY(save->isEnabled());
    QVERIFY(details->toPlainText().contains("UTF-16LE")); QVERIFY(details->toPlainText().contains("CRLF 1"));
    QVERIFY(page.grab().save("file-recovery-preview.png"));
    filePage->exportCopy(destination); QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 5000);
    QVERIFY(status->text().contains("Created copy"));
    { QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), mojibake::ByteEncodingAnalyzer::bom(mojibake::Encoding::Utf8) + text.toUtf8()); }
    filePage->exportCopy(source); QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 5000);
    QVERIFY(status->text().contains("never overwritten"));
    { QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); }
    QVERIFY(settings.setLocale("zh_CN")); QCoreApplication::processEvents();
    QCOMPARE(page.findChild<ElaPushButton*>("mojibakeFileMode")->text(), QString::fromUtf8("文本文件"));
    QCOMPARE(save->text(), QString::fromUtf8("另存修复副本…"));
    page.findChild<ElaPushButton*>("mojibakeTextMode")->click(); QCOMPARE(pasted->toPlainText(), QString("preserve pasted input"));
    const auto large = dir.filePath("large.txt"), largeCopy = dir.filePath("large-copy.txt");
    const QByteArray bytes(100 * 1024, 'a');
    { QFile file(large); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes); }
    filePage->openFile(large); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QVERIFY(preview->toPlainText().size() <= 32768); QVERIFY(details->toPlainText().contains(QString::fromUtf8("完整候选")));
    filePage->exportCopy(largeCopy); QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 5000);
    { QFile file(largeCopy); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); }
}
void TestUi::filenameRecoveryWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("names-ui.ini")); QVERIFY(settings.setLocale("zh_CN"));
    const auto root = dir.filePath("source"), output = dir.filePath("repaired");
    QVERIFY(QDir().mkpath(root + "/浣犲ソ"));
    { QFile f(root + "/浣犲ソ/浣犲ソ.txt"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("bytes stay unchanged\r\n"); }
    { QFile f(root + "/浣犲ソ/other.txt"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("other"); }
    MojibakePage page(nullptr, &settings); page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(1000, 1000); page.show();
    auto* mode = page.findChild<ElaPushButton*>("mojibakeNameMode"); QCOMPARE(mode->text(), QString("文件与文件夹名")); mode->click();
    auto* names = page.findChild<FilenameRecoveryPage*>(); QVERIFY(names);
    auto* rescan = page.findChild<ElaPushButton*>("filenameRescan");
    auto* exportButton = page.findChild<ElaPushButton*>("filenameExport");
    auto* tree = page.findChild<QTreeView*>("filenameTree");
    auto* details = page.findChild<ElaPlainTextEdit*>("filenameDetails");
    auto* field = page.findChild<ElaLineEdit*>("filenameProposedName");
    auto* status = page.findChild<QLabel*>("filenameStatus");
    names->scanDirectory(root); QTRY_VERIFY_WITH_TIMEOUT(rescan->isEnabled(), 5000); QVERIFY(exportButton->isEnabled());
    const auto folder = tree->model()->index(0, 0); QCOMPARE(folder.data().toString(), QString("浣犲ソ"));
    auto choose = [&page](const QString& name) {
        for (int i = 1; i <= 5; ++i) {
            auto* b = page.findChild<ElaPushButton*>(QString("filenameCandidate%1").arg(i));
            if (b->isEnabled() && b->toolTip() == name) { b->click(); return true; }
        }
        return false;
    };
    tree->setCurrentIndex(folder); QVERIFY(choose("你好"));
    QModelIndex file;
    for (int i = 0; i < tree->model()->rowCount(folder); ++i) {
        const auto index = tree->model()->index(i, 0, folder);
        if (index.data().toString() == "浣犲ソ.txt") file = index;
    }
    QVERIFY(file.isValid()); tree->setCurrentIndex(file); QVERIFY(choose("你好.txt"));
    QVERIFY(details->toPlainText().contains("你好/你好.txt"));
    field->setText("other.txt"); page.findChild<ElaPushButton*>("filenameApplyName")->click();
    QVERIFY(!exportButton->isEnabled()); QVERIFY(details->toPlainText().contains("名称冲突"));
    field->setText("../escape"); page.findChild<ElaPushButton*>("filenameApplyName")->click(); QVERIFY(!exportButton->isEnabled());
    QVERIFY(choose("你好.txt")); QVERIFY(exportButton->isEnabled());
    QVERIFY(page.grab().save("filename-recovery-preview.png"));
    names->exportCopy(output); QTRY_VERIFY_WITH_TIMEOUT(rescan->isEnabled(), 5000);
    QVERIFY2(status->text().contains("已创建目录副本"), qPrintable(status->text()));
    { QFile f(output + "/你好/你好.txt"); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("bytes stay unchanged\r\n")); }
    QVERIFY(QFileInfo::exists(root + "/浣犲ソ/浣犲ソ.txt")); QVERIFY(!QFileInfo::exists(root + "/你好"));
    names->exportCopy(output); QTRY_VERIFY_WITH_TIMEOUT(rescan->isEnabled(), 5000); QVERIFY(status->text().contains("输出已存在"));
    QVERIFY(settings.setLocale("en")); QCoreApplication::processEvents(); QCOMPARE(mode->text(), QString("File and folder names"));
    QCOMPARE(tree->model()->headerData(1, Qt::Horizontal).toString(), QString("Proposed name"));
}
void TestUi::clipboardSourceWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("clipboard-ui.ini")); QVERIFY(settings.setLocale("zh_CN"));
    MojibakePage page(nullptr, &settings); page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(1000, 900); page.show();
    auto* mode = page.findChild<ElaPushButton*>("mojibakeClipboardMode"); QCOMPARE(mode->text(), QString("剪贴板")); mode->click();
    auto* clipboardPage = page.findChild<ClipboardSourcePage*>(); QVERIFY(clipboardPage);
    auto* input = page.findChild<ElaPlainTextEdit*>("mojibakeInput"); input->setPlainText("keep current input");
    auto* read = page.findChild<ElaPushButton*>("clipboardRead");
    auto* use = page.findChild<ElaPushButton*>("clipboardUse");
    auto* plain = page.findChild<ElaPushButton*>("clipboardPlain");
    auto* html = page.findChild<ElaPushButton*>("clipboardHtml");
    auto* details = page.findChild<ElaPlainTextEdit*>("clipboardDetails");
    auto* status = page.findChild<QLabel*>("clipboardStatus");
    QVERIFY(!use->isEnabled()); QVERIFY(status->text().contains("不会自动监听"));
    QMimeData mime; mime.setText("浣犲ソ"); mime.setHtml("<p>你好</p>"); mime.setData("text/rtf", "{not parsed}");
    clipboardPage->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
    QVERIFY(use->isEnabled()); QVERIFY(plain->isChecked()); QVERIFY(!html->isChecked());
    QVERIFY(status->text().contains("内容不同")); QVERIFY(details->toPlainText().contains("暂不解析")); QCOMPARE(input->toPlainText(), QString("keep current input"));
    html->click(); QVERIFY(html->isChecked()); QVERIFY(!plain->isChecked());
    QVERIFY(page.grab().save("clipboard-source-preview.png"));
    use->click(); QCOMPARE(input->toPlainText(), QString("你好")); QCOMPARE(page.findChild<QStackedWidget*>("mojibakeSources")->currentIndex(), 0);
    QVERIFY(!page.findChild<ElaPushButton*>("mojibakeCandidate1")->isEnabled()); // Reading/importing does not start analysis.
    auto* analyze = page.findChild<ElaPushButton*>("mojibakeAnalyze"); analyze->click(); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(page.findChild<ElaPlainTextEdit*>("mojibakeOutput")->toPlainText(), QString("你好"));
    mode->click(); plain->click(); use->click(); QCOMPARE(input->toPlainText(), QString("浣犲ソ"));
    analyze->click(); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000); QCOMPARE(page.findChild<ElaPlainTextEdit*>("mojibakeOutput")->toPlainText(), QString("你好"));
    mode->click(); mime.setText(QString("ä½ å¥½")); mime.setHtml("<p>ä½&nbsp;å¥½</p>"); clipboardPage->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
    QVERIFY(details->toPlainText().contains("相同的 Unicode")); html->click(); use->click();
    QVERIFY(input->document()->toRawText().contains(QChar(0xa0))); analyze->click(); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QCOMPARE(page.findChild<ElaPlainTextEdit*>("mojibakeOutput")->toPlainText(), QString("你好"));
    mode->click(); mime.clear(); mime.setHtml("<p>HTML only</p>"); clipboardPage->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
    QVERIFY(!plain->isEnabled()); QVERIFY(html->isChecked()); QVERIFY(use->isEnabled());
    mime.clear(); mime.setData("application/x-unknown", "opaque"); clipboardPage->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000); QVERIFY(!use->isEnabled());
    mime.setText(QString(100 * 1024, 'a')); clipboardPage->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
    QVERIFY(page.findChild<ElaPlainTextEdit*>("clipboardPlainPreview")->toPlainText().size() <= 32768); QVERIFY(details->toPlainText().contains("完整文本"));
    use->click(); QCOMPARE(input->toPlainText().size(), 100 * 1024);
    mode->click(); page.findChild<ElaPushButton*>("clipboardClear")->click(); QVERIFY(!use->isEnabled()); QCOMPARE(input->toPlainText().size(), 100 * 1024);
    QVERIFY(settings.setLocale("en")); QCoreApplication::processEvents(); QCOMPARE(mode->text(), QString("Clipboard")); QCOMPARE(read->text(), QString("Read clipboard"));
    page.resize(640, 480); QCoreApplication::processEvents(); QVERIFY(page.grab().save("clipboard-source-small.png"));
    // A worker only owns captured values; destroying a page during extraction is safe.
    auto* closing = new ClipboardSourcePage(); mime.setHtml("<p>" + QString(200 * 1024, 'x') + "</p>"); closing->inspect(mime); delete closing;
}
void TestUi::diagnosticReportWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("report-ui.ini")); QVERIFY(settings.setLocale("zh_CN"));
    MojibakePage page(nullptr, &settings); page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(1000, 1100); page.show();
    auto* input = page.findChild<ElaPlainTextEdit*>("mojibakeInput"); auto* analyze = page.findChild<ElaPushButton*>("mojibakeAnalyze");
    auto* report = page.findChild<DiagnosticReportWidget*>("textDiagnosticReport"); QVERIFY(report);
    auto* exportButton = report->findChild<ElaPushButton*>("diagnosticExport"); QVERIFY(!exportButton->isEnabled());
    input->setPlainText("浣犲ソ"); analyze->click(); QTRY_VERIFY_WITH_TIMEOUT(analyze->isEnabled(), 5000);
    QVERIFY(exportButton->isEnabled()); QVERIFY(report->findChild<QLabel*>("diagnosticSummary")->text().contains("Unicode 文本"));
    const auto destination = dir.filePath("diagnostic.json"); QVERIFY(report->exportReport(destination));
    QVERIFY(report->findChild<QLabel*>("diagnosticExportStatus")->isVisible());
    { QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); QVERIFY(!bytes.contains(QString("浣犲ソ").toUtf8())); QCOMPARE(QJsonDocument::fromJson(bytes).object()["selected_candidate"].toInt(), 1); }
    page.findChild<ElaPushButton*>("mojibakeCandidate2")->click(); QVERIFY(report->exportReport(dir.filePath("alternative.json")));
    { QFile file(dir.filePath("alternative.json")); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["selected_candidate"].toInt(), 2); }
    QVERIFY(!report->exportReport(destination)); input->setPlainText("changed"); QVERIFY(!exportButton->isEnabled());
    const auto source = dir.filePath("utf8.txt"); { QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QString("正常文字").toUtf8()); }
    page.findChild<ElaPushButton*>("mojibakeFileMode")->click(); page.findChild<TextFileRecoveryPage*>()->openFile(source);
    QTRY_VERIFY_WITH_TIMEOUT(page.findChild<ElaPushButton*>("fileRecoveryAnalyze")->isEnabled(), 5000);
    auto* fileReport = page.findChild<DiagnosticReportWidget*>("fileDiagnosticReport"); QVERIFY(fileReport->findChild<QLabel*>("diagnosticSummary")->text().contains("原始文件字节"));
    QVERIFY(!fileReport->exportReport(source));
    page.findChild<ElaPushButton*>("mojibakeClipboardMode")->click(); QMimeData mime; mime.setText("normal");
    page.findChild<ClipboardSourcePage*>()->inspect(mime); QTRY_VERIFY_WITH_TIMEOUT(page.findChild<ElaPushButton*>("clipboardRead")->isEnabled(), 5000);
    auto* clipboardReport = page.findChild<DiagnosticReportWidget*>("clipboardDiagnosticReport");
    QVERIFY(clipboardReport->findChild<QLabel*>("diagnosticSummary")->text().contains("尚未运行"));
    page.findChild<ElaPushButton*>("clipboardClear")->click(); QVERIFY(!clipboardReport->findChild<ElaPushButton*>("diagnosticExport")->isEnabled());
    page.findChild<ElaPushButton*>("mojibakeNameMode")->click(); const auto root = dir.filePath("names"); QVERIFY(QDir().mkdir(root));
    { QFile file(root + "/浣犲ソ.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("body"); }
    page.findChild<FilenameRecoveryPage*>()->scanDirectory(root); QTRY_VERIFY_WITH_TIMEOUT(page.findChild<ElaPushButton*>("filenameRescan")->isEnabled(), 5000);
    auto* nameReport = page.findChild<DiagnosticReportWidget*>("nameDiagnosticReport"); QVERIFY(nameReport->exportReport(dir.filePath("names.json")));
    auto* field = page.findChild<ElaLineEdit*>("filenameProposedName"); field->setText("CON.txt"); page.findChild<ElaPushButton*>("filenameApplyName")->click();
    QVERIFY(nameReport->findChild<QLabel*>("diagnosticSummary")->text().contains("操作受阻"));
    QVERIFY(page.grab().save("diagnostic-report-preview.png"));
    QVERIFY(settings.setLocale("en")); QCoreApplication::processEvents(); QCOMPARE(nameReport->findChild<ElaPushButton*>("diagnosticExport")->text(), QString("Export diagnostic report…"));
    QVERIFY(nameReport->findChild<QLabel*>("diagnosticSummary")->text().contains("Blocked"));
}
void TestUi::textStyleWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("style-ui.ini")); QVERIFY(settings.setLocale("en"));
    ToolRegistry tools; ToolDescriptor descriptor; descriptor.id = "text.style_converter"; descriptor.name = "Text Style Converter";
    int loads = 0; descriptor.factory = [&](QWidget* parent) { ++loads; return new TextStylePage(parent, &settings); }; QVERIFY(tools.registerTool(descriptor));
    NavigationManager navigation; QVERIFY(navigation.addGroup({"encoding", "Text and Encoding", 10})); QVERIFY(navigation.addItem({descriptor.id, "encoding", 30}));
    MainWindow window(settings,tools,navigation); window.setAttribute(Qt::WA_DontShowOnScreen); window.resize(1100,900); window.show();
    auto* page = window.findChild<TextStylePage*>(); QVERIFY(page); QCOMPARE(loads,1);
    QString copiedPayload; page->setClipboardWriter([&](const QString& value) { copiedPayload = value; return true; });
    auto* input = page->findChild<ElaPlainTextEdit*>("textStyleInput"); auto* output = page->findChild<ElaPlainTextEdit*>("textStyleOutput");
    auto* convert = page->findChild<ElaPushButton*>("textStyleConvert"); auto* dice = page->findChild<ElaIconButton*>("textStyleRandomSeed"); QVERIFY(dice);
    QVERIFY(!page->findChild<ElaPushButton*>("textStyleRegenerate"));
    auto* seed = page->findChild<ElaLineEdit*>("textStyleSeed"); auto* presets = page->findChild<ElaComboBox*>("textStylePreset");
    auto* copy = page->findChild<ElaPushButton*>("textStyleCopy"); auto* randomConvert = page->findChild<ElaPushButton*>("textStyleRandomConvert"); QVERIFY(randomConvert);
    QVERIFY(!page->findChild<ElaPushButton*>("textStyleAdvanced")); QVERIFY(!page->findChild<ElaPushButton*>("textStyleCopyParameters"));
    QVERIFY(!convert->isEnabled()); QVERIFY(output->isReadOnly()); QVERIFY(!seed->text().isEmpty());
    input->setPlainText("你好，我喜欢你。ABC 😀"); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000);
    QVERIFY(!seed->text().isEmpty()); const auto originalSeed = seed->text(); const auto first = output->document()->toRawText(); QVERIFY(copy->isEnabled());
    convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QCOMPARE(output->document()->toRawText(),first); QCOMPARE(seed->text(),originalSeed);
    dice->click(); QVERIFY(seed->text()!=originalSeed); QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!copy->isEnabled());
    convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000);
    const auto beforeRoll = seed->text(); randomConvert->click(); QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(),5000);
    QVERIFY(seed->text() != beforeRoll); QVERIFY(!output->toPlainText().isEmpty());
    const auto rolledOutput = output->document()->toRawText(); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QCOMPARE(output->document()->toRawText(), rolledOutput);
    QCoreApplication::processEvents();
    auto* styles = page->findChild<ElaComboBox*>("textStyleStyle");
    QCOMPARE(styles->mapTo(page, QPoint()).y(), presets->mapTo(page, QPoint()).y());
    QCOMPARE(convert->mapTo(page, QPoint()).y(), copy->mapTo(page, QPoint()).y());
    seed->setText("18446744073709551615"); QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!copy->isEnabled()); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000);
    copy->click(); QCOMPARE(copiedPayload,output->document()->toRawText()); const auto retainedOutput = output->document()->toRawText();
    page->setClipboardWriter([](const QString&) { return false; }); copy->click(); QVERIFY(page->findChild<QLabel*>("textStyleStatus")->text().contains("retry copying"));
    page->setClipboardWriter([&](const QString& value) { copiedPayload = value; return true; });
    copy->click();
    QVERIFY(settings.setLocale("zh_CN")); QCoreApplication::processEvents(); QCOMPARE(convert->text(),QString("转换")); QCOMPARE(output->document()->toRawText(),retainedOutput);
    QCOMPARE(window.getNavigationNodeTitle(window.pageKey(descriptor.id)),QString("文本风格转换"));
    eTheme->setThemeMode(ElaThemeType::Dark); QTest::qWait(250); QCOMPARE(output->document()->toRawText(),retainedOutput); QVERIFY(window.grab().save("text-style-dark.png"));
    QVERIFY(settings.setLocale("zh_TW")); QCoreApplication::processEvents(); QCOMPARE(convert->text(),QString("轉換")); QCOMPARE(output->document()->toRawText(),retainedOutput);
    eTheme->setThemeMode(ElaThemeType::Light); QTest::qWait(250); QVERIFY(window.grab().save("text-style-light.png"));
    window.resize(640,480);
    QTRY_VERIFY(convert->mapTo(page, QPoint()).y() >= input->mapTo(page, QPoint()).y() + input->height());
    QTRY_VERIFY(copy->mapTo(page, QPoint()).y() >= output->mapTo(page, QPoint()).y() + output->height());
    QTest::qWait(100); QVERIFY(window.grab().save("text-style-small.png"));
    seed->clear(); QVERIFY(!convert->isEnabled()); QVERIFY(randomConvert->isEnabled()); seed->insert("-1"); QVERIFY(seed->text().isEmpty());
    seed->insert("中文"); QVERIFY(seed->text().isEmpty()); seed->insert("18446744073709551616"); QVERIFY(seed->text().isEmpty());
    QTest::keyClicks(seed, "CAT42"); QCOMPARE(seed->text(), QString("cat42"));
    convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); const auto namedOutput = output->document()->toRawText();
    QCOMPARE(seed->text(), QString("cat42"));
    page->findChild<ElaPushButton*>("textStyleCopySeed")->click(); QCOMPARE(copiedPayload, QString("cat42"));
    seed->setText("CaT42"); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QCOMPARE(output->document()->toRawText(), namedOutput);
    seed->selectAll(); seed->insert("bad!"); QCOMPARE(seed->text(), QString("cat42"));
    seed->setText("00042"); presets->setCurrentIndex(presets->findData("heavy")); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QCOMPARE(seed->text(),QString("42"));
    page->findChild<ElaPushButton*>("textStyleClear")->click(); QVERIFY(input->toPlainText().isEmpty()); QCOMPARE(seed->text(),QString("42")); QCOMPARE(presets->currentData().toString(),QString("heavy"));
    QVERIFY(!convert->isEnabled()); QVERIFY(!copy->isEnabled());
    input->setPlainText(" \t\n"); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QCOMPARE(output->toPlainText(),QString(" \t\n"));
    input->setPlainText("<script>&ABC🙂</script>"); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QVERIFY(output->toPlainText().contains("<script>&ABC🙂</script>"));
    input->setPlainText(QString(100*1024,'x')); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); copy->click(); QCOMPARE(copiedPayload,output->document()->toRawText()); QVERIFY(copiedPayload.size()>=100*1024);
    auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
    page->setRunner([entered,release](const QString& text, auto style, const QString& preset, uint64_t s, const textstyle::Control&) {
        entered->release(); release->acquire(); return textstyle::Engine::transform(text,style,preset,s);
    });
    input->setPlainText("late old input"); convert->click(); QTRY_VERIFY_WITH_TIMEOUT(entered->available()>0,5000); entered->acquire(); input->setPlainText("new current input");
    release->release(); QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QVERIFY(output->toPlainText().isEmpty()); QCOMPARE(input->toPlainText(),QString("new current input"));
    convert->click(); QTRY_VERIFY_WITH_TIMEOUT(entered->available()>0,5000); entered->acquire(); page->findChild<ElaPushButton*>("textStyleCancel")->click(); release->release();
    QTRY_VERIFY_WITH_TIMEOUT(convert->isEnabled(),5000); QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!copy->isEnabled());
    auto ended = std::make_shared<QSemaphore>(); auto* closing = new TextStylePage();
    closing->setRunner([entered,release,ended](const QString& text, auto style, const QString& preset, uint64_t s, const textstyle::Control& control) {
        entered->release(); release->acquire(); auto result = textstyle::Engine::transform(text,style,preset,s,control); ended->release(); return result;
    });
    closing->findChild<ElaPlainTextEdit*>("textStyleInput")->setPlainText("close while running"); closing->findChild<ElaPushButton*>("textStyleConvert")->click();
    QTRY_VERIFY_WITH_TIMEOUT(entered->available()>0,5000); entered->acquire(); delete closing; release->release(); QTRY_VERIFY_WITH_TIMEOUT(ended->available()>0,5000);
    settings.sync(); TextStylePage fresh(nullptr,&settings); QVERIFY(fresh.findChild<ElaPlainTextEdit*>("textStyleInput")->toPlainText().isEmpty());
    QVERIFY(!fresh.findChild<ElaLineEdit*>("textStyleSeed")->text().isEmpty());
    QCOMPARE(fresh.findChild<ElaComboBox*>("textStylePreset")->currentData().toString(),QString("heavy"));
}
void TestUi::characterArtWorkflow()
{
    QTemporaryDir dir; AppSettings settings(dir.filePath("art.ini")); QVERIFY(settings.setLocale("en"));
    ToolDescriptor descriptor; descriptor.id="image.character_art"; descriptor.name="Character Art Generator";
    int loads=0; descriptor.factory=[&loads](QWidget* parent) { ++loads; return new CharacterArtPage(parent); };
    LazyToolPage lazy(descriptor); lazy.setAttribute(Qt::WA_DontShowOnScreen); lazy.resize(1050,900);
    QCOMPARE(loads,0); lazy.show(); QTRY_COMPARE(loads,1);
    auto* page=qobject_cast<CharacterArtPage*>(lazy.page()); QVERIFY(page);
    auto* generate=page->findChild<ElaPushButton*>("artGenerate"); auto* copy=page->findChild<ElaPushButton*>("artCopy");
    auto* columns=page->findChild<ElaSpinBox*>("artColumns"); auto* zoom=page->findChild<ElaSpinBox*>("artZoom");
    QVERIFY(generate && copy && columns && zoom); QVERIFY(!generate->isEnabled()); QVERIFY(!copy->isEnabled());
    QImage source(400,300,QImage::Format_RGB32); source.fill(Qt::white);
    { QPainter painter(&source); painter.setBrush(Qt::black); painter.drawEllipse(100,50,200,200); }
    const auto inputPath=dir.filePath("circle.png"); QVERIFY(source.save(inputPath)); page->openImage(inputPath);
    QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(),3000); generate->click(); QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(),3000);
    QVERIFY(page->result().success()); QCOMPARE(page->result().columns,80); const auto text=page->result().text;
    QString copied; page->setClipboardWriter([&copied](const QString& value) { copied=value; return true; }); copy->click(); QCOMPARE(copied,text);
    QVERIFY(page->saveText(dir.filePath("result.txt"))); QVERIFY(page->savePng(dir.filePath("result.png")));
    QFile file(dir.filePath("result.txt")); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),text.toUtf8());
    QVERIFY(!page->saveText(dir.filePath("result.txt"))); QVERIFY(!QImage(dir.filePath("result.png")).isNull());
    zoom->setValue(50); QCOMPARE(page->result().text,text);
    eTheme->setThemeMode(ElaThemeType::Dark); QCOMPARE(page->result().text,text); QVERIFY(page->grab().save("character-art-dark.png"));
    QVERIFY(settings.setLocale("zh_CN")); QCoreApplication::processEvents(); QCOMPARE(generate->text(),QStringLiteral("生成")); QCOMPARE(page->result().text,text);
    QVERIFY(settings.setLocale("zh_TW")); QCoreApplication::processEvents(); QCOMPARE(generate->text(),QStringLiteral("產生"));
    QVERIFY(settings.setLocale("en")); columns->setValue(100); QVERIFY(!page->result().success()); QVERIFY(!copy->isEnabled());
    generate->click(); page->findChild<ElaPushButton*>("artCancel")->click(); QTRY_VERIFY(generate->isEnabled()); QVERIFY(!page->result().success());
    generate->click(); QTRY_VERIFY(copy->isEnabled()); page->findChild<ElaPushButton*>("artClear")->click(); QVERIFY(!generate->isEnabled()); QVERIFY(!copy->isEnabled());
    page->openImage(dir.filePath("missing.png")); QTRY_VERIFY(page->findChild<ElaPushButton*>("artOpen")->isEnabled()); QVERIFY(!generate->isEnabled());
    page->openImage(inputPath); QTRY_VERIFY(generate->isEnabled()); generate->click(); QTRY_VERIFY(copy->isEnabled());
    lazy.resize(560,850); QTest::qWait(100); QVERIFY(page->grab().save("character-art-small.png"));
    lazy.hide(); lazy.show(); QCOMPARE(loads,1); QVERIFY(page->result().success());
    auto* palette=page->findChild<ElaComboBox*>("artPalette"); auto* custom=page->findChild<ElaLineEdit*>("artCustomCharacters");
    QVERIFY(palette && custom); palette->setCurrentIndex(2); QVERIFY(custom->isVisible()); QVERIFY(!copy->isEnabled());
    custom->setText(QStringLiteral("春夏秋冬")); QVERIFY(generate->isEnabled()); generate->click(); QTRY_VERIFY(copy->isEnabled());
    QVERIFY(page->savePng(dir.filePath("custom.png"))); QVERIFY(page->grab().save("character-art-custom.png"));
    custom->setText(QStringLiteral("A\U0010ffff")); QVERIFY(!generate->isEnabled()); QVERIFY(!copy->isEnabled());
    QVERIFY(page->findChild<QLabel*>("artStatus")->text().contains(QStringLiteral("\U0010ffff")));
    palette->setCurrentIndex(0); QVERIFY(generate->isEnabled()); QVERIFY(!custom->isVisible());
}
void TestUi::voiceDesignerWorkflow()
{
    QTemporaryDir dir; TestHttpServer server; AppSettings settings(dir.filePath("voice.ini"));
    QVERIFY(settings.setLocale("en")); QVERIFY(settings.setAiConfiguration({server.url(), "text-model", "test-only-key"}));
    ToolRegistry tools; ToolDescriptor descriptor;
    descriptor.id = "speech.voice_designer"; descriptor.name = "Voice Designer";
    int loads = 0;
    descriptor.factory = [&loads, &settings](QWidget* parent) { ++loads; return new VoiceDesignerPage(parent, &settings); };
    QVERIFY(tools.registerTool(descriptor));
    LazyToolPage lazy(descriptor); lazy.setAttribute(Qt::WA_DontShowOnScreen); lazy.resize(1050, 750);
    QCOMPARE(loads, 0); lazy.show(); QTRY_COMPARE(loads, 1);
    auto* page = qobject_cast<VoiceDesignerPage*>(lazy.page()); QVERIFY(page);
    auto* description = page->findChild<ElaPlainTextEdit*>("voiceDescription");
    auto* preview = page->findChild<ElaPlainTextEdit*>("voicePreviewText");
    auto* generate = page->findChild<ElaPushButton*>("voiceGenerate");
    auto* cancel = page->findChild<ElaPushButton*>("voiceCancel");
    auto* clear = page->findChild<ElaPushButton*>("voiceClear");
    auto* status = page->findChild<QLabel*>("voiceStatus");
    QVERIFY(description && preview && generate && cancel && clear && status);
    QVERIFY(!generate->isEnabled()); QVERIFY(!cancel->isEnabled()); QCOMPARE(server.requests, 0);
    description->setPlainText(QStringLiteral("温暖、沉稳的成年声音。")); preview->setPlainText(QStringLiteral("你好\nHello\u00a0world!"));
    QVERIFY(generate->isEnabled()); QCOMPARE(server.requests, 0);
    const QByteArray audio = QByteArray::fromHex("524946460402000057415645666d74201000000001000100c05d000080bb00000200100064617461e0010000") + QByteArray(480, '\0');
    QVERIFY(speech::MiMoTtsService::validWav(audio));
    server.response = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{
        {"audio", QJsonObject{{"data", QString::fromLatin1(audio.toBase64())}}}}}, {"finish_reason", "stop"}}}}}).toJson();
    generate->click(); QTRY_VERIFY_WITH_TIMEOUT(page->resultIsCurrent(), 3000);
    QCOMPARE(page->result()->audioData, audio); QCOMPARE(server.requests, 1);
    QCOMPARE(page->result()->snapshot.previewText, QStringLiteral("你好\nHello\u00a0world!"));
    // No voice API saved yet: the text address and key are borrowed, with the default voice model.
    QCOMPARE(QJsonDocument::fromJson(server.lastBody).object()["model"].toString(), AppSettings::defaultVoiceModel());
    auto* connection = page->findChild<QLabel*>("voiceConnection"); QVERIFY(connection);
    QVERIFY(connection->text().contains("not saved")); QVERIFY(connection->text().contains(AppSettings::defaultVoiceModel()));
    QVERIFY(status->text().contains("validated")); QVERIFY(status->text().contains("24000 Hz"));
    auto* play = page->findChild<ElaPushButton*>("voicePlay");
    auto* stop = page->findChild<ElaPushButton*>("voiceStop");
    auto* save = page->findChild<ElaPushButton*>("voiceSave");
    auto* time = page->findChild<QLabel*>("voiceTime");
    auto* notice = page->findChild<QLabel*>("voiceNotice");
    QVERIFY(play && stop && save && time && notice);
    QVERIFY(play->isEnabled()); QVERIFY(save->isEnabled()); QVERIFY(!stop->isEnabled());
    QCOMPARE(time->text(), QStringLiteral("0:00 / 0:00")); // 10 ms of silence
    // Playback starts asynchronously; a host without an audio device reports an error instead.
    play->click();
    QTRY_VERIFY_WITH_TIMEOUT(page->isPlaying() || notice->isVisible(), 5000);
    if (page->isPlaying()) {
        QCOMPARE(play->text(), QStringLiteral("Pause")); QVERIFY(stop->isEnabled());
        stop->click(); QTRY_VERIFY(!page->isPlaying()); QCOMPARE(play->text(), QStringLiteral("Play"));
    }
    QVERIFY(page->resultIsCurrent());
    const auto wavPath = dir.filePath("voice.wav");
    QVERIFY(page->saveWav(wavPath)); QVERIFY(notice->isVisible()); QVERIFY(notice->text().contains("voice.wav"));
    QFile saved(wavPath); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), audio); saved.close();
    QVERIFY(!page->saveWav(wavPath)); QVERIFY(notice->text().contains("never overwritten")); // Existing files are kept.
    QFile kept(wavPath); QVERIFY(kept.open(QIODevice::ReadOnly)); QCOMPARE(kept.readAll(), audio); kept.close();
    settings.setThemeMode(ElaThemeType::Dark); eTheme->setThemeMode(ElaThemeType::Dark);
    QVERIFY(page->resultIsCurrent()); QVERIFY(page->grab().save("voice-designer-dark.png"));
    QVERIFY(settings.setLocale("zh_CN")); QCoreApplication::processEvents();
    QCOMPARE(generate->text(), QStringLiteral("生成试听")); QVERIFY(page->resultIsCurrent());
    QVERIFY(status->text().contains(QStringLiteral("校验")));
    QVERIFY(settings.setLocale("zh_TW")); QCoreApplication::processEvents(); QCOMPARE(generate->text(), QStringLiteral("產生試聽"));
    QVERIFY(settings.setLocale("en"));
    QCoreApplication::processEvents(); QCOMPARE(play->text(), QStringLiteral("Play"));
    play->click(); QTRY_VERIFY_WITH_TIMEOUT(page->isPlaying() || notice->isVisible(), 5000);
    description->setPlainText("new description"); QVERIFY(!page->isPlaying()); // Editing stops stale playback.
    QVERIFY(!page->resultIsCurrent()); QVERIFY(status->text().contains("out of date"));
    QVERIFY(!play->isEnabled()); QVERIFY(!save->isEnabled()); QVERIFY(!notice->isVisible()); QVERIFY(!page->saveWav(dir.filePath("stale.wav")));
    QVERIFY(!QFile::exists(dir.filePath("stale.wav")));
    server.hold = true; generate->click(); QTRY_COMPARE(server.requests, 2); QVERIFY(cancel->isEnabled()); QVERIFY(!generate->isEnabled());
    preview->appendPlainText("edit during request"); QTRY_VERIFY(generate->isEnabled()); QVERIFY(!cancel->isEnabled()); QVERIFY(!page->resultIsCurrent());
    generate->click(); QTRY_COMPARE(server.requests, 3); cancel->click(); QTRY_VERIFY(generate->isEnabled()); QVERIFY(status->text().contains("cancelled"));
    generate->click(); QTRY_COMPARE(server.requests, 4);
    QVERIFY(settings.setAiConfiguration({server.url(), "another-text-model", "other-test-key"}));
    QTRY_VERIFY(generate->isEnabled()); QVERIFY(!cancel->isEnabled()); QCOMPARE(server.requests, 4);
    // Once saved, the voice API is independent: its own model and key, unaffected by text API changes.
    QVERIFY(settings.setVoiceConfiguration({server.url(), "custom-voicedesign", "voice-test-key"}));
    QVERIFY(connection->text().contains("custom-voicedesign")); QVERIFY(!connection->text().contains("not saved"));
    generate->click(); QTRY_COMPARE(server.requests, 5);
    QCOMPARE(QJsonDocument::fromJson(server.lastBody).object()["model"].toString(), QString("custom-voicedesign"));
    QVERIFY(server.lastHeaders.contains("Bearer voice-test-key"));
    QVERIFY(settings.setAiConfiguration({server.url(), "text-again", "text-test-key"})); QCoreApplication::processEvents();
    QVERIFY(cancel->isEnabled());
    QVERIFY(settings.setVoiceConfiguration({server.url(), "custom-voicedesign", "voice-test-key"}));
    QTRY_VERIFY(!cancel->isEnabled()); QVERIFY(generate->isEnabled());
    generate->click(); QTRY_COMPARE(server.requests, 6); lazy.hide(); QTRY_VERIFY(!cancel->isEnabled());
    lazy.show(); QCOMPARE(loads, 1); QCOMPARE(server.requests, 6);
    server.hold = false; server.status = 400; server.response = "private text and secret";
    generate->click(); QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("400"), 3000);
    QVERIFY(!status->text().contains("private")); QVERIFY(generate->isEnabled());
    clear->click(); QVERIFY(description->toPlainText().isEmpty()); QVERIFY(preview->toPlainText().isEmpty());
    QVERIFY(!page->result()); QVERIFY(!generate->isEnabled());
    lazy.resize(540, 720); description->setPlainText("small window"); preview->setPlainText("hello");
    QTRY_VERIFY(preview->mapTo(page, QPoint()).y() > description->mapTo(page, QPoint(0, description->height())).y());
    QTest::qWait(100); QVERIFY(page->grab().save("voice-designer-small.png"));
    QVERIFY(!page->findChild<QScrollArea*>()->horizontalScrollBar()->isVisible());
}
void TestUi::textStyleRestoreAiWorkflow()
{
    QTemporaryDir dir; TestHttpServer server; AppSettings settings(dir.filePath("style-ai.ini")); QVERIFY(settings.setLocale("en"));
    QVERIFY(settings.setAiConfiguration({server.url(),"model","test-only-key"}));
    TextStylePage page(nullptr,&settings); page.setAttribute(Qt::WA_DontShowOnScreen); page.resize(1100,850); page.show();
    auto* input=page.findChild<ElaPlainTextEdit*>("textStyleInput"); auto* output=page.findChild<ElaPlainTextEdit*>("textStyleOutput");
    auto* run=page.findChild<ElaPushButton*>("textStyleConvert"); auto* aiRun=page.findChild<ElaPushButton*>("textStyleAiRun");
    auto* local=page.findChild<ElaPushButton*>("textStyleLocalView"); auto* aiView=page.findChild<ElaPushButton*>("textStyleAiView");
    auto* status=page.findChild<QLabel*>("textStyleAiStatus");
    QString clipboard; page.setClipboardWriter([&](const QString& text) { clipboard=text; return true; });
    QCOMPARE(server.requests,0); page.findChild<ElaPushButton*>("textStyleRestoreMode")->click();
    QVERIFY(page.findChild<ElaLineEdit*>("textStyleSeed")->isHidden() || !page.findChild<ElaLineEdit*>("textStyleSeed")->isVisible());
    input->setPlainText("☆莪愛沵☆\n妑"); run->click(); QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(),5000);
    QVERIFY(!output->toPlainText().contains("☆")); QVERIFY(output->toPlainText().contains("妑")); const auto conservative=output->toPlainText();
    auto* choice=page.findChild<ElaPushButton*>("textStyleRestoreCandidate1"); QVERIFY(choice->isEnabled()); choice->click(); QVERIFY(output->toPlainText()!=conservative);
    page.findChild<ElaPushButton*>("textStyleRestoreCandidate0")->click(); QCOMPARE(output->toPlainText(),conservative);
    QCOMPARE(server.requests,0); server.response=TestHttpServer::success("我爱你\n把"); aiRun->click();
    QTRY_VERIFY_WITH_TIMEOUT(aiView->isEnabled(),5000); QCOMPARE(output->toPlainText(),QString("我爱你\n把"));
    QCOMPARE(server.requests,1); const auto body=QJsonDocument::fromJson(server.lastBody).object();
    const auto messages=body["messages"].toArray(); const auto payload=QJsonDocument::fromJson(messages.last().toObject()["content"].toString().toUtf8()).object();
    QCOMPARE(payload["task"].toString(),QString("text_style_restore")); QCOMPARE(payload["original_text"].toString(),QString("☆莪愛沵☆\n妑")); QVERIFY(!payload["ambiguities"].toArray().isEmpty());
    local->click(); QCOMPARE(output->toPlainText(),conservative); aiView->click();
    page.findChild<ElaPushButton*>("textStyleCopy")->click(); QCOMPARE(clipboard,QString("我爱你\n把"));
    input->setPlainText("new input"); QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!aiView->isEnabled());
    server.hold=true; aiRun->click(); QTRY_COMPARE_WITH_TIMEOUT(server.requests,2,3000); input->setPlainText("changed during AI");
    QTRY_VERIFY_WITH_TIMEOUT(aiRun->isEnabled(),3000); QVERIFY(output->toPlainText().isEmpty()); QVERIFY(!aiView->isEnabled());
    aiRun->click(); QTRY_COMPARE_WITH_TIMEOUT(server.requests,3,3000); page.findChild<ElaPushButton*>("textStyleAiCancel")->click();
    QTRY_VERIFY_WITH_TIMEOUT(aiRun->isEnabled(),3000); QVERIFY(status->text().contains("cancelled"));
    server.hold=false; server.response=TestHttpServer::success("styled AI"); page.findChild<ElaPushButton*>("textStyleEncodeMode")->click();
    input->setPlainText("你好"); aiRun->click(); QTRY_VERIFY_WITH_TIMEOUT(aiView->isEnabled(),5000);
    const auto encodeBody=QJsonDocument::fromJson(server.lastBody).object(); const auto encodeMessages=encodeBody["messages"].toArray();
    QCOMPARE(QJsonDocument::fromJson(encodeMessages.last().toObject()["content"].toString().toUtf8()).object()["task"].toString(),QString("text_style_encode"));
    QVERIFY(!local->isEnabled());
    input->setPlainText(QString(16*1024+1,'x')); QVERIFY(!aiRun->isEnabled());
    input->setPlainText("莪愛沵"); page.findChild<ElaPushButton*>("textStyleRestoreMode")->click(); run->click(); QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(),5000);
    server.status=400; aiRun->click(); QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("HTTP 400"),3000); QVERIFY(local->isEnabled()); QVERIFY(!output->toPlainText().isEmpty());
    QVERIFY(settings.setLocale("zh_CN")); QCoreApplication::processEvents(); QCOMPARE(run->text(),QString("本地还原")); QCOMPARE(aiRun->text(),QString("AI 还原"));
    QVERIFY(page.grab().save("text-style-restore.png"));
    page.resize(400,650);
    QTRY_VERIFY(output->mapTo(&page,QPoint()).y()>=run->mapTo(&page,QPoint()).y()+run->height());
    QTRY_VERIFY(page.findChild<ElaPushButton*>("textStyleRestoreCandidate2")->mapTo(&page,QPoint()).y()>page.findChild<ElaPushButton*>("textStyleRestoreCandidate0")->mapTo(&page,QPoint()).y());
    QTest::qWait(100);
    QVERIFY(page.grab().save("text-style-restore-small.png"));
}
void TestUi::windowWorkflow()
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath("settings.ini"));
    settings.setThemeMode(ElaThemeType::Dark);
    QVERIFY(settings.setLocale("en"));
    eTheme->setThemeMode(settings.themeMode());
    ToolRegistry tools;
    int firstLoads = 0, secondLoads = 0;
    ToolDescriptor tool;
    tool.id = "encoding.base64"; tool.name = "Base64"; tool.icon = ElaIconType::Code;
    tool.factory = [&firstLoads](QWidget* parent) { ++firstLoads; return new Base64Page(parent); };
    QVERIFY(tools.registerTool(tool));
    tool.id = "test.second"; tool.name = "Second";
    tool.factory = [&secondLoads](QWidget* parent) { ++secondLoads; return new Base64Page(parent); };
    QVERIFY(tools.registerTool(tool));
    NavigationManager navigationConfig;
    QVERIFY(navigationConfig.addGroup({"encoding", "Text and Encoding", 10}));
    QVERIFY(navigationConfig.addGroup({"test", "Test Tools", 20}));
    QVERIFY(navigationConfig.addItem({"encoding.base64", "encoding", 10}));
    QVERIFY(navigationConfig.addItem({"test.second", "test", 10}));
    MainWindow window(settings, tools, navigationConfig);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    QCOMPARE(firstLoads, 0);
    QCOMPARE(secondLoads, 0);
    QVERIFY(!window.groupKey("encoding").isEmpty());
    QCOMPARE(window.getNavigationNodeTitle(window.groupKey("encoding")), QString("Text and Encoding"));
    QVERIFY(window.getNavigationNodeIsExpanded(window.groupKey("encoding")));
    window.show();
    QTest::qWait(100);
    QCOMPARE(firstLoads, 1);
    QCOMPARE(secondLoads, 0);
    window.collapseNavigationNode(window.groupKey("encoding"));
    QVERIFY(!window.getNavigationNodeIsExpanded(window.groupKey("encoding")));
    QTest::qWait(350);
    window.expandNavigationNode(window.groupKey("encoding"));
    QTest::qWait(350);
    auto* page = window.findChild<LazyToolPage*>("encoding.base64");
    QVERIFY(page && page->page());
    auto* input = page->findChild<ElaPlainTextEdit*>("inputText");
    auto* output = page->findChild<ElaPlainTextEdit*>("outputText");
    auto* encode = page->findChild<ElaPushButton*>("encodeButton");
    QVERIFY(input && output && encode);
    const auto initialSize = window.size();
    for (const auto size : {QSize(640, 480), QSize(800, 600), QSize(1400, 900)}) {
        window.resize(size);
        QCoreApplication::processEvents();
        QVERIFY(input->height() > 0 && output->height() > 0);
        QVERIFY(encode->isVisible());
        const auto inputBounds = QRect(input->mapTo(page, QPoint()), input->size());
        const auto outputBounds = QRect(output->mapTo(page, QPoint()), output->size());
        const auto buttonBounds = QRect(encode->mapTo(page, QPoint()), encode->size());
        QVERIFY(!inputBounds.intersects(buttonBounds));
        QVERIFY(!outputBounds.intersects(buttonBounds));
        auto* scroll = page->findChild<QScrollArea*>();
        QVERIFY(scroll);
        scroll->ensureWidgetVisible(encode);
        const auto center = encode->mapTo(scroll->viewport(), encode->rect().center());
        QVERIFY(scroll->viewport()->rect().contains(center));
    }
    window.resize(initialSize);
    page->findChild<ElaPushButton*>("clearButton")->click();
    QVERIFY(input->toPlainText().isEmpty() && output->toPlainText().isEmpty());
    encode->click();
    QCOMPARE(page->findChild<QLabel*>("statusLabel")->text(), QString("Input is empty"));
    input->setPlainText(QString::fromUtf8("Hello 世界"));
    encode->click();
    QCOMPARE(output->toPlainText(), QString("SGVsbG8g5LiW55WM"));
    const auto result = output->toPlainText();
    auto* tree = window.findChild<QTreeView*>("ElaNavigationView");
    QVERIFY(tree);
    const auto groupIndex = tree->model()->index(0, 0);
    const auto childIndex = tree->model()->index(0, 0, groupIndex);
    QVERIFY(groupIndex.isValid() && childIndex.isValid());
    // Mouse clicks must commit visibility immediately, even during an earlier
    // arrow/fade animation, without reloading or navigating away from the page.
    for (int i = 0; i < 12; ++i) {
        const bool expanded = tree->isExpanded(groupIndex);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(groupIndex).center());
        QCOMPARE(tree->isExpanded(groupIndex), !expanded);
        QCOMPARE(tree->visualRect(childIndex).isEmpty(), expanded);
        QCOMPARE(window.getCurrentNavigationPageKey(), window.pageKey("encoding.base64"));
        QCOMPARE(settings.lastPage(), QString("encoding.base64"));
        QCOMPARE(firstLoads, 1);
        QCOMPARE(input->toPlainText(), QString::fromUtf8("Hello 世界"));
        QCOMPARE(output->toPlainText(), result);
        QTest::qWait(10);
    }
    auto* bar = window.findChild<ElaAppBar*>();
    QVERIFY(bar);
    auto* hamburger = bar->findChild<ElaToolButton*>("sidebarToggle");
    auto* theme = bar->findChild<ElaToolButton*>("themeToggle");
    auto* pin = bar->findChild<ElaToolButton*>("alwaysOnTopToggle");
    auto* minimize = bar->findChild<ElaToolButton*>("minimizeButton");
    auto* maximize = bar->findChild<ElaToolButton*>("maximizeButton");
    QVERIFY(hamburger && theme && pin && minimize && maximize);
    QVERIFY(hamburger->isVisible());
    QCOMPARE(hamburger->mapTo(bar, QPoint()).x(), 0);
    QVERIFY(theme->mapTo(bar, QPoint()).x() < pin->mapTo(bar, QPoint()).x());
    QVERIFY(pin->mapTo(bar, QPoint()).x() < minimize->mapTo(bar, QPoint()).x());
    QCOMPARE(theme->mapTo(bar, QPoint()).y(), minimize->mapTo(bar, QPoint()).y());
    auto* navigation = window.findChild<ElaNavigationBar*>();
    QVERIFY(navigation);
    hamburger->click();
    QTest::qWait(350);
    QCOMPARE(navigation->getDisplayMode(), ElaNavigationType::Compact);
    theme->click();
    QTest::qWait(300);
    QCOMPARE(eTheme->getThemeMode(), ElaThemeType::Light);
    QCOMPARE(settings.themeMode(), ElaThemeType::Light);
    QCOMPARE(input->toPlainText(), QString::fromUtf8("Hello 世界"));
    QCOMPARE(output->toPlainText(), result);
    pin->click();
    QVERIFY(window.getIsStayTop());
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows")
        QVERIFY(GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_EXSTYLE) & WS_EX_TOPMOST);
#endif
    pin->click();
    QVERIFY(!window.getIsStayTop());
    window.navigation(window.pageKey("test.second"));
    QTest::qWait(100);
    QCOMPARE(secondLoads, 1);
    window.navigation(window.pageKey("settings"));
    QTest::qWait(100);
    QCOMPARE(window.getCurrentNavigationPageKey(), window.pageKey("settings"));
    auto* settingsPage = window.findChild<SettingsPage*>();
    QVERIFY(!settingsPage->findChild<ElaComboBox*>("themeCombo"));
    theme->click();
    QTest::qWait(300);
    QCOMPARE(eTheme->getThemeMode(), ElaThemeType::Dark);
    QVERIFY(settings.setLocale("zh_CN"));
    QCoreApplication::processEvents();
    QCOMPARE(window.getNavigationNodeTitle(window.pageKey("settings")), QString::fromUtf8("设置"));
    QCOMPARE(window.getNavigationNodeTitle(window.groupKey("encoding")), QString::fromUtf8("文本与编码"));
    QCOMPARE(encode->text(), QString::fromUtf8("编码"));
    QVERIFY(settings.setLocale("zh_TW"));
    QCoreApplication::processEvents();
    QCOMPARE(encode->text(), QString::fromUtf8("編碼"));
    QCOMPARE(window.getNavigationNodeTitle(window.groupKey("encoding")), QString::fromUtf8("文字與編碼"));
    QVERIFY(settings.setLocale("en"));
    QCoreApplication::processEvents();
    QCOMPARE(encode->text(), QString("Encode"));
    window.navigation(window.pageKey("encoding.base64"));
    QTest::qWait(100);
    QCOMPARE(firstLoads, 1);
    QCOMPARE(input->toPlainText(), QString::fromUtf8("Hello 世界"));
    QCOMPARE(output->toPlainText(), result);
    input->setPlainText("invalid!");
    page->findChild<ElaPushButton*>("decodeButton")->click();
    QVERIFY(output->toPlainText().isEmpty());
    QCOMPARE(page->findChild<QLabel*>("statusLabel")->text(), QString("Invalid Base64 input"));
    input->setPlainText("SGVsbG8=");
    page->findChild<ElaPushButton*>("decodeButton")->click();
    QCOMPARE(output->toPlainText(), QString("Hello"));
    hamburger->click();
    QTest::qWait(350);
    window.removeNavigationNode(window.groupKey("test"));
    QVERIFY(settings.setLocale("zh_CN"));
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save("rebuilt-dark.png"));
    settings.setThemeMode(ElaThemeType::Light);
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save("rebuilt-light.png"));
    window.navigation(window.pageKey("settings"));
    QTest::qWait(100);
    QVERIFY(window.grab().save("rebuilt-settings.png"));
    window.close();
    QVERIFY(!settings.windowGeometry().isEmpty());
    QCOMPARE(settings.lastPage(), QString("settings"));
    QVERIFY(settings.navigationExpanded());
    AppSettings restored(dir.filePath("settings.ini"));
    MainWindow reopened(restored, tools, navigationConfig);
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    reopened.show();
    QTest::qWait(100);
    QCOMPARE(reopened.getCurrentNavigationPageKey(), reopened.pageKey("settings"));
    QCOMPARE(reopened.size(), window.size());
    QCOMPARE(restored.themeMode(), ElaThemeType::Light);
    reopened.close();
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    eApp->init();
    ui::initializeTheme(&app);
    TestUi test;
    return QTest::qExec(&test, argc, argv);
}
#include "TestUi.moc"

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
#include "TestHttpServer.h"
#include <QTemporaryDir>
#include <QLabel>
#include <QClipboard>
#include <QStyleHints>
#include <QSettings>
#include <QLocale>
#include <QScrollArea>
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
    window.findChild<ElaPushButton*>("mojibakeCopy")->click();
    const auto alternative = QApplication::clipboard()->text();
    first->click();
    QVERIFY(first->isChecked() && !second->isChecked());
    QCOMPARE(output->toPlainText(), QString::fromUtf8("你好"));
    window.findChild<ElaPushButton*>("mojibakeCopy")->click();
    QCOMPARE(QApplication::clipboard()->text(), QString::fromUtf8("你好"));
    QVERIFY(alternative != QApplication::clipboard()->text());
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
    page.findChild<ElaPushButton*>("mojibakeAiCopy")->click();
    QCOMPARE(QApplication::clipboard()->text(), QString::fromUtf8("你好！"));
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

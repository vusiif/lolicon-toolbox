#include <QtTest>
#include <QMimeData>
#include <QTextDocument>
#include "ClipboardSource.h"
using namespace mojibake;
class TestClipboardSource : public QObject {
    Q_OBJECT
private slots:
    void plainAndHtml();
    void htmlTextOnly();
    void formatsAreNotFetched();
    void emptyInvalidAndLimits();
};
void TestClipboardSource::plainAndHtml() {
    QMimeData mime; mime.setText("浣犲ソ"); mime.setHtml("<p>你好</p>");
    const auto snapshot = ClipboardSource::extract(ClipboardSource::capture(mime));
    QVERIFY(snapshot.plainUsable()); QVERIFY(snapshot.htmlUsable());
    QCOMPARE(snapshot.capture.plain, QString("浣犲ソ")); QCOMPARE(snapshot.htmlText, QString("你好")); QVERIFY(snapshot.capture.html.isEmpty());
    QVERIFY(snapshot.capture.formats.contains("text/plain")); QVERIFY(snapshot.capture.formats.contains("text/html"));
    mime.setText("now changed"); QCOMPARE(snapshot.capture.plain, QString("浣犲ソ")); // Immutable captured values.
    QMimeData htmlOnly; htmlOnly.setHtml("<p>你好</p>");
    const auto only = ClipboardSource::extract(ClipboardSource::capture(htmlOnly)); QVERIFY(!only.plainUsable()); QVERIFY(only.htmlUsable());
    mime.setText("你好"); QCOMPARE(ClipboardSource::extract(ClipboardSource::capture(mime)).capture.plain, snapshot.htmlText);
}
void TestClipboardSource::htmlTextOnly() {
    QMimeData mime; mime.setHtml("<html><head><style>.x{color:red}</style><script>do_not_keep_me()</script></head><body><p>&lt;tag&gt;&amp;&nbsp;你好<br>第二行 😀</p></body></html>");
    const auto snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.htmlUsable());
    QCOMPARE(snapshot.htmlText, QString("<tag>& 你好\n第二行 😀"));
    QVERIFY(!snapshot.htmlText.contains("do_not_keep_me")); QVERIFY(!snapshot.htmlText.contains("color:red"));
    int fallbackCalls = 0; const auto previous = QTextDocument::defaultResourceProvider();
    QTextDocument::setDefaultResourceProvider([&](const QUrl&) { ++fallbackCalls; return QVariant("external content"); });
    mime.setHtml("<p>text<img src='file:///does-not-exist.png'><img src='https://example.invalid/image.png'></p>");
    const auto objects = ClipboardSource::extract(ClipboardSource::capture(mime));
    QTextDocument::setDefaultResourceProvider(previous);
    QCOMPARE(fallbackCalls, 0); QVERIFY(objects.htmlText.startsWith("text")); QVERIFY(objects.htmlText.contains(QChar::ObjectReplacementCharacter));
}
void TestClipboardSource::formatsAreNotFetched() {
    class OfferedMime final : public QMimeData {
    public: mutable QStringList fetched;
    protected:
        QVariant retrieveData(const QString& format, QMetaType type) const override {
            fetched << format; return QMimeData::retrieveData(format, type);
        }
    } mime;
    mime.setText("normal"); mime.setData("text/rtf", "{not parsed}"); mime.setData("application/x-private-format", "never read");
    const auto snapshot = ClipboardSource::extract(ClipboardSource::capture(mime));
    QVERIFY(snapshot.capture.hasRtf); QVERIFY(snapshot.capture.formats.contains("application/x-private-format"));
    QVERIFY(!mime.fetched.contains("text/rtf")); QVERIFY(!mime.fetched.contains("application/x-private-format"));
}
void TestClipboardSource::emptyInvalidAndLimits() {
    QMimeData mime; auto snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(!snapshot.plainUsable()); QVERIFY(!snapshot.htmlUsable());
    mime.setText(""); mime.setHtml(""); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(!snapshot.plainUsable()); QVERIFY(!snapshot.htmlUsable());
    mime.setText(QString(QChar(0xd800))); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.capture.plainInvalid); QVERIFY(!snapshot.plainUsable());
    mime.setText(QString(ClipboardSource::MaxBytes, 'a')); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.plainUsable()); QCOMPARE(snapshot.capture.plain.size(), ClipboardSource::MaxBytes);
    mime.setText(QString(ClipboardSource::MaxBytes + 1, 'a')); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.capture.plainTooLarge); QVERIFY(!snapshot.plainUsable());
    mime.setText(QString(ClipboardSource::MaxBytes / 2, QChar(0x4f60))); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.capture.plainTooLarge);
    mime.setHtml("<p>" + QString(ClipboardSource::MaxBytes, 'a') + "</p>"); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.capture.htmlTooLarge); QVERIFY(!snapshot.htmlUsable());
    mime.setText("�"); mime.setHtml("<p>�</p>"); snapshot = ClipboardSource::extract(ClipboardSource::capture(mime)); QVERIFY(snapshot.plainUsable()); QVERIFY(snapshot.htmlUsable()); QCOMPARE(snapshot.htmlText, QString("�"));
}
QTEST_MAIN(TestClipboardSource)
#include "TestClipboardSource.moc"

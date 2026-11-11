#include <QTest>
#include "Base64Codec.h"

namespace {
class TestBase64Codec : public QObject
{
    Q_OBJECT

private slots:
    void encodeEmpty();
    void encodeHello();
    void encodeHelloWorld();
    void encodeUtf8();

    void decodeEmpty();
    void decodeHello();
    void decodeHelloWorld();
    void decodeUtf8();

    void decodeInvalid_data();
    void decodeInvalid();

    void encodeDecodeRoundtrip();
};

void TestBase64Codec::encodeEmpty()
{
    QCOMPARE(Base64Codec::encode(""), "");
}

void TestBase64Codec::encodeHello()
{
    QCOMPARE(Base64Codec::encode("Hello"), "SGVsbG8=");
}

void TestBase64Codec::encodeHelloWorld()
{
    QCOMPARE(Base64Codec::encode("hello world"), "aGVsbG8gd29ybGQ=");
}

void TestBase64Codec::encodeUtf8()
{
    const std::string result = Base64Codec::encode("你好");
    QCOMPARE(result, "5L2g5aW9");
}

void TestBase64Codec::decodeEmpty()
{
    const auto result = Base64Codec::decode("");
    QVERIFY(result.has_value());
    QCOMPARE(result.value(), "");
}

void TestBase64Codec::decodeHello()
{
    const auto result = Base64Codec::decode("SGVsbG8=");
    QVERIFY(result.has_value());
    QCOMPARE(result.value(), "Hello");
}

void TestBase64Codec::decodeHelloWorld()
{
    const auto result = Base64Codec::decode("aGVsbG8gd29ybGQ=");
    QVERIFY(result.has_value());
    QCOMPARE(result.value(), "hello world");
}

void TestBase64Codec::decodeUtf8()
{
    const auto result = Base64Codec::decode("5L2g5aW9");
    QVERIFY(result.has_value());
    QCOMPARE(result.value(), "你好");
}

void TestBase64Codec::decodeInvalid_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("description");

    QTest::newRow("invalid chars") << "SGVs!bG8=" << "contains invalid character !";
    QTest::newRow("all padding") << "====" << "all padding characters";
    QTest::newRow("invalid length one") << "S" << "length not divisible by four";
    QTest::newRow("invalid length three") << "SGV" << "missing final character";
    QTest::newRow("invalid padding") << "SGVsbG8==" << "extra padding";
}

void TestBase64Codec::decodeInvalid()
{
    QFETCH(QString, input);
    QFETCH(QString, description);
    Q_UNUSED(description);

    const auto result = Base64Codec::decode(input.toStdString());
    QVERIFY(!result.has_value());
}

void TestBase64Codec::encodeDecodeRoundtrip()
{
    QStringList testCases = {
        "",
        "Hello",
        "hello world",
        "你好",
        "Hello, World! 123",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
    };

    for (const auto& original : testCases) {
        std::string encoded = Base64Codec::encode(original.toStdString());
        auto decoded = Base64Codec::decode(encoded);
        QVERIFY2(decoded.has_value(), qPrintable("Failed to decode: " + QString::fromStdString(encoded)));
        QCOMPARE(decoded.value(), original.toStdString());
    }
}

}  // namespace
QTEST_MAIN(TestBase64Codec)
#include "TestBase64Codec.moc"

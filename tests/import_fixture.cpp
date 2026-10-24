#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QThread>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--dump-single-json")) {
        QTextStream(stdout)
            << R"({"title":"取消测试","formats":[{"format_id":"test","ext":"m4a","acodec":"aac","vcodec":"none","abr":192}]})"
            << Qt::endl;
        return 0;
    }
    const int output = args.indexOf("-o");
    const auto path = output >= 0 ? args.value(output + 1) : args.last();
    const auto source = qEnvironmentVariable("BILI_TEST_DOWNLOAD_SOURCE");
    if (output >= 0 && !source.isEmpty())
        return QFile::copy(source, path) ? 0 : 3;
    for (const auto &suffix : {QString(), QString(".part"), QString(".ytdl"), QString(".part-Frag1")}) {
        QFile file(path + suffix);
        if (!file.open(QIODevice::WriteOnly))
            return 2;
        file.write("partial test output");
    }
    QThread::sleep(30);
    return 0;
}

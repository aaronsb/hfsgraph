#include "filetypestyle.h"

#include <QHash>
#include <QMimeDatabase>
#include <QMimeType>

namespace ui {

namespace {
// Lowercase filename extension (without the dot), or empty if none. The single
// keying rule shared by both the icon and the colour lookups.
QString extensionOf(const QString &name) {
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.mid(dot + 1).toLower() : QString();
}
} // namespace

QIcon fileTypeIcon(const QString &name) {
    static QMimeDatabase db;
    static QHash<QString, QIcon> cache;
    const QString suffix = extensionOf(name);
    const auto it = cache.constFind(suffix);
    if (it != cache.constEnd())
        return it.value();
    const QMimeType mt = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    QIcon icon = QIcon::fromTheme(mt.iconName());
    if (icon.isNull())
        icon = QIcon::fromTheme(mt.genericIconName());
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    cache.insert(suffix, icon);
    return icon;
}

QColor fileTypeColor(const QString &name) {
    static QMimeDatabase db;
    static QHash<QString, QColor> cache;
    const QString ext = extensionOf(name);
    const auto hit = cache.constFind(ext);
    if (hit != cache.constEnd())
        return hit.value();

    // Deliberate palette, grouped so related types read alike at a glance.
    static const QColor kSource(0x6E, 0xC6, 0x8A);  // green   — code
    static const QColor kHeader(0x4F, 0xB0, 0x9C);  // teal    — headers / build
    static const QColor kData(0xE0, 0xB0, 0x4D);    // amber   — json / yaml / config
    static const QColor kMarkup(0xE0, 0x7B, 0x53);  // orange  — markdown / html / text
    static const QColor kImage(0xC8, 0x6F, 0xD6);   // magenta — images
    static const QColor kAudio(0xD9, 0x66, 0x8A);   // pink    — audio
    static const QColor kVideo(0xD0, 0x5A, 0x5A);   // red     — video
    static const QColor kArchive(0xB0, 0x8A, 0x5A); // brown   — archives
    static const QColor kDoc(0x5A, 0x9B, 0xD0);     // blue    — documents
    static const QColor kBinary(0x9A, 0x9A, 0xA6);  // grey    — binaries / objects
    static const QHash<QString, QColor> kByExt = {
        {QStringLiteral("c"), kSource},    {QStringLiteral("cc"), kSource},
        {QStringLiteral("cpp"), kSource},  {QStringLiteral("cxx"), kSource},
        {QStringLiteral("py"), kSource},   {QStringLiteral("rs"), kSource},
        {QStringLiteral("go"), kSource},   {QStringLiteral("java"), kSource},
        {QStringLiteral("js"), kSource},   {QStringLiteral("ts"), kSource},
        {QStringLiteral("rb"), kSource},   {QStringLiteral("sh"), kSource},
        {QStringLiteral("h"), kHeader},    {QStringLiteral("hpp"), kHeader},
        {QStringLiteral("hxx"), kHeader},  {QStringLiteral("cmake"), kHeader},
        {QStringLiteral("mk"), kHeader},   {QStringLiteral("json"), kData},
        {QStringLiteral("yaml"), kData},   {QStringLiteral("yml"), kData},
        {QStringLiteral("toml"), kData},   {QStringLiteral("ini"), kData},
        {QStringLiteral("xml"), kData},    {QStringLiteral("conf"), kData},
        {QStringLiteral("cfg"), kData},    {QStringLiteral("md"), kMarkup},
        {QStringLiteral("html"), kMarkup}, {QStringLiteral("htm"), kMarkup},
        {QStringLiteral("css"), kMarkup},  {QStringLiteral("rst"), kMarkup},
        {QStringLiteral("txt"), kMarkup},  {QStringLiteral("png"), kImage},
        {QStringLiteral("jpg"), kImage},   {QStringLiteral("jpeg"), kImage},
        {QStringLiteral("gif"), kImage},   {QStringLiteral("svg"), kImage},
        {QStringLiteral("webp"), kImage},  {QStringLiteral("bmp"), kImage},
        {QStringLiteral("ico"), kImage},   {QStringLiteral("mp3"), kAudio},
        {QStringLiteral("wav"), kAudio},   {QStringLiteral("flac"), kAudio},
        {QStringLiteral("ogg"), kAudio},   {QStringLiteral("mp4"), kVideo},
        {QStringLiteral("mkv"), kVideo},   {QStringLiteral("mov"), kVideo},
        {QStringLiteral("webm"), kVideo},  {QStringLiteral("zip"), kArchive},
        {QStringLiteral("tar"), kArchive}, {QStringLiteral("gz"), kArchive},
        {QStringLiteral("xz"), kArchive},  {QStringLiteral("bz2"), kArchive},
        {QStringLiteral("7z"), kArchive},  {QStringLiteral("zst"), kArchive},
        {QStringLiteral("rar"), kArchive}, {QStringLiteral("pdf"), kDoc},
        {QStringLiteral("doc"), kDoc},     {QStringLiteral("docx"), kDoc},
        {QStringLiteral("odt"), kDoc},     {QStringLiteral("o"), kBinary},
        {QStringLiteral("a"), kBinary},    {QStringLiteral("so"), kBinary},
        {QStringLiteral("bin"), kBinary},  {QStringLiteral("exe"), kBinary},
        {QStringLiteral("class"), kBinary}};

    QColor c;
    const auto ce = kByExt.constFind(ext);
    if (ce != kByExt.constEnd()) {
        c = ce.value();
    } else {
        // Unlisted: a stable hue hashed from the mime type, so related-but-uncurated
        // types still cluster and the choice is reproducible run to run.
        const QMimeType mt = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
        const QString key = mt.isValid() ? mt.name() : ext;
        const int hue = static_cast<int>(qHash(key) % 360u);
        c = QColor::fromHsl(hue, 130, 150);
    }
    cache.insert(ext, c);
    return c;
}

} // namespace ui

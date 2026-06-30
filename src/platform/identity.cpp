#include "platform/identity.h"

#include <QByteArray>
#include <QFile>
#include <QUuid>

#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>

namespace platform {

namespace {

// Encode a QString path for the C xattr/stat APIs. QFile::encodeName honours the
// filesystem encoding (UTF-8 on Linux) and yields a NUL-terminated buffer.
QByteArray enc(const QString &path) {
    return QFile::encodeName(path);
}

} // namespace

QString readDurableId(const QString &path) {
    const QByteArray p = enc(path);
    // Two-step: ask the size, then fetch. A 64-byte stack guess covers a UUID without a
    // heap round-trip in the common case; fall back to the reported size otherwise.
    char buf[64];
    ssize_t n = ::getxattr(p.constData(), kDurableIdAttr, buf, sizeof(buf));
    if (n >= 0)
        return QString::fromUtf8(buf, static_cast<int>(n));
    if (errno != ERANGE)
        return QString(); // ENODATA (unstamped), ENOTSUP (no xattrs), ENOENT, EACCES …
    // Attr is larger than our guess — query the real length and fetch exactly.
    n = ::getxattr(p.constData(), kDurableIdAttr, nullptr, 0);
    if (n <= 0)
        return QString();
    QByteArray big(static_cast<int>(n), Qt::Uninitialized);
    n = ::getxattr(p.constData(), kDurableIdAttr, big.data(), big.size());
    return n >= 0 ? QString::fromUtf8(big.constData(), static_cast<int>(n)) : QString();
}

bool stampDurableId(const QString &path, const QString &id) {
    const QByteArray p = enc(path);
    const QByteArray v = id.toUtf8();
    return ::setxattr(p.constData(), kDurableIdAttr, v.constData(), v.size(), 0) == 0;
}

bool xattrSupported(const QString &path) {
    const QByteArray p = enc(path);
    char buf[64];
    const ssize_t n = ::getxattr(p.constData(), kDurableIdAttr, buf, sizeof(buf));
    if (n >= 0 || errno == ERANGE)
        return true;            // attribute present (so the fs supports it)
    return errno != ENOTSUP;    // ENODATA/ENOENT/EACCES still imply xattr support
}

QString newDurableId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

core::Fingerprint statFingerprint(const QString &path) {
    core::Fingerprint fp;
    struct stat st {};
    if (::lstat(enc(path).constData(), &st) == 0) {
        fp.dev = static_cast<quint64>(st.st_dev);
        fp.ino = static_cast<quint64>(st.st_ino);
        fp.mtime = static_cast<qint64>(st.st_mtime);
        fp.size = static_cast<qint64>(st.st_size);
        fp.valid = true;
    }
    return fp;
}

QString ensureDurableId(core::FsNode &node) {
    // Stamp / read the *on-disk* location, not `path`: in a projection a moved node's
    // `path` is the staged (non-existent) destination, while `originalPath` is where the
    // directory actually lives. They coincide on a freshly scanned node. (Scanned nodes
    // always carry originalPath; fall back to path for a bare node.)
    const QString onDisk = node.originalPath.isEmpty() ? node.path : node.originalPath;
    // Disk is the authority, not `node.identity`: the projection pins a *path* into
    // `identity` as a fallback key (deepCopy), so a non-empty `identity` doesn't prove the
    // dir is stamped. Re-read the xattr — adopt an existing id, mint one only when absent.
    QString id = readDurableId(onDisk);
    if (id.isEmpty()) {
        id = newDurableId();
        if (!stampDurableId(onDisk, id))
            return QString(); // fs rejected the write — caller stays path-keyed
    }
    node.identity = id; // adopt the durable on-disk id (idempotent on repeat calls)
    return id;
}

} // namespace platform

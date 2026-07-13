// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 CouchPlay Contributors

#include "SystemOps.h"
#include "PolkitActions.h"

#include <QDir>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>

#ifdef HAVE_POLKITQT
#include <PolkitQt1/Authority>
#include <PolkitQt1/Subject>
#endif

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mount.h>
#include <sys/acl.h>
#include <unistd.h>

namespace {
class ScopedFd
{
public:
    explicit ScopedFd(int fd = -1)
        : m_fd(fd)
    {
    }

    ~ScopedFd()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;

    int get() const { return m_fd; }

    int release()
    {
        int fd = m_fd;
        m_fd = -1;
        return fd;
    }

    void reset(int fd)
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
        m_fd = fd;
    }

private:
    int m_fd;
};

bool relativeComponents(const QString &relativePath, QStringList *components)
{
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)
        || relativePath.contains(QLatin1Char('\0'))) {
        return false;
    }

    QString cleaned = QDir::cleanPath(relativePath);
    if (cleaned != relativePath || cleaned == QStringLiteral(".")
        || cleaned == QStringLiteral("..")
        || cleaned.startsWith(QStringLiteral("../"))) {
        return false;
    }

    QStringList parts = cleaned.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
    }

    *components = parts;
    return true;
}

int openBaseDirectory(const QString &baseDirectory, uid_t expectedOwner)
{
    QByteArray base = QFile::encodeName(baseDirectory);
    int fd = ::open(base.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != expectedOwner) {
        ::close(fd);
        errno = EPERM;
        return -1;
    }

    return fd;
}

int openDirectoryChain(const QString &baseDirectory, const QStringList &components,
                       uid_t owner, gid_t group, mode_t mode, bool create)
{
    ScopedFd current(openBaseDirectory(baseDirectory, owner));
    if (current.get() < 0) {
        return -1;
    }

    for (const QString &component : components) {
        QByteArray name = QFile::encodeName(component);
        int next = ::openat(current.get(), name.constData(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool created = false;
        if (next < 0 && errno == ENOENT && create) {
            if (::mkdirat(current.get(), name.constData(), mode) != 0) {
                return -1;
            }
            next = ::openat(current.get(), name.constData(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            created = true;
        }
        if (next < 0) {
            return -1;
        }

        ScopedFd checked(next);
        struct stat st;
        if (::fstat(checked.get(), &st) != 0 || !S_ISDIR(st.st_mode)
            || (!created && st.st_uid != owner)) {
            errno = EPERM;
            return -1;
        }
        if (created && (::fchown(checked.get(), owner, group) != 0
                        || ::fchmod(checked.get(), mode) != 0)) {
            return -1;
        }
        current.reset(checked.release());
    }

    return current.release();
}

bool writeAll(int fd, const QByteArray &content)
{
    qsizetype offset = 0;
    while (offset < content.size()) {
        ssize_t written = ::write(fd, content.constData() + offset,
                                  static_cast<size_t>(content.size() - offset));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        offset += written;
    }
    return true;
}

bool setAclEntry(const QString &path, uid_t pathOwner, acl_tag_t entryType,
                 id_t qualifierValue, bool read, bool write, bool execute,
                 bool remove)
{
    QByteArray encodedPath = QFile::encodeName(path);
    ScopedFd fd(::open(encodedPath.constData(), O_PATH | O_CLOEXEC | O_NOFOLLOW));
    if (fd.get() < 0) {
        return false;
    }

    struct stat st;
    if (::fstat(fd.get(), &st) != 0 || st.st_uid != pathOwner) {
        errno = EPERM;
        return false;
    }

    QByteArray fdPath = QByteArrayLiteral("/proc/self/fd/") + QByteArray::number(fd.get());
    acl_t acl = ::acl_get_file(fdPath.constData(), ACL_TYPE_ACCESS);
    if (!acl) {
        return false;
    }

    acl_entry_t matchingEntry = nullptr;
    acl_entry_t entry = nullptr;
    int entryId = ACL_FIRST_ENTRY;
    while (::acl_get_entry(acl, entryId, &entry) == 1) {
        entryId = ACL_NEXT_ENTRY;
        acl_tag_t tag;
        if (::acl_get_tag_type(entry, &tag) != 0 || tag != entryType) {
            continue;
        }
        auto *qualifier = static_cast<id_t *>(::acl_get_qualifier(entry));
        if (qualifier && *qualifier == qualifierValue) {
            matchingEntry = entry;
        }
        if (qualifier) {
            ::acl_free(qualifier);
        }
        if (matchingEntry) {
            break;
        }
    }

    bool ok = true;
    if (remove) {
        if (matchingEntry && ::acl_delete_entry(acl, matchingEntry) != 0) {
            ok = false;
        }
    } else {
        if (!matchingEntry && ::acl_create_entry(&acl, &matchingEntry) != 0) {
            ok = false;
        }
        if (ok && (::acl_set_tag_type(matchingEntry, entryType) != 0
                   || ::acl_set_qualifier(matchingEntry, &qualifierValue) != 0)) {
            ok = false;
        }

        acl_permset_t permissions;
        if (ok && (::acl_get_permset(matchingEntry, &permissions) != 0
                   || ::acl_clear_perms(permissions) != 0)) {
            ok = false;
        }
        if (ok && read && ::acl_add_perm(permissions, ACL_READ) != 0) {
            ok = false;
        }
        if (ok && write && ::acl_add_perm(permissions, ACL_WRITE) != 0) {
            ok = false;
        }
        if (ok && execute && ::acl_add_perm(permissions, ACL_EXECUTE) != 0) {
            ok = false;
        }
    }

    if (ok && (::acl_calc_mask(&acl) != 0
               || ::acl_set_file(fdPath.constData(), ACL_TYPE_ACCESS, acl) != 0)) {
        ok = false;
    }
    ::acl_free(acl);
    return ok;
}
}

RealSystemOps::RealSystemOps(QObject *parent)
    : QObject(parent)
{
}

struct passwd *RealSystemOps::getpwnam(const char *name)
{
    return ::getpwnam(name);
}

struct passwd *RealSystemOps::getpwuid(uid_t uid)
{
    return ::getpwuid(uid);
}

struct group *RealSystemOps::getgrnam(const char *name)
{
    return ::getgrnam(name);
}

bool RealSystemOps::fileExists(const QString &path)
{
    return QFile::exists(path);
}

bool RealSystemOps::isDirectory(const QString &path)
{
    return QFileInfo(path).isDir();
}

bool RealSystemOps::mkpath(const QString &path)
{
    return QDir().mkpath(path);
}

bool RealSystemOps::removeFile(const QString &path)
{
    return QFile::remove(path);
}

bool RealSystemOps::copyFile(const QString &source, const QString &dest)
{
    return QFile::copy(source, dest);
}

bool RealSystemOps::writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    qint64 written = file.write(content);
    file.close();
    return written == content.size();
}

bool RealSystemOps::createDirectoryBeneath(const QString &baseDirectory,
                                           const QString &relativePath,
                                           uid_t owner, gid_t group, mode_t mode)
{
    QStringList components;
    if (!relativeComponents(relativePath, &components)) {
        return false;
    }

    ScopedFd directory(openDirectoryChain(baseDirectory, components, owner, group, mode, true));
    return directory.get() >= 0;
}

bool RealSystemOps::writeFileBeneath(const QString &baseDirectory,
                                     const QString &relativePath,
                                     const QByteArray &content,
                                     uid_t owner, gid_t group, mode_t mode)
{
    QStringList components;
    if (!relativeComponents(relativePath, &components) || components.isEmpty()) {
        return false;
    }

    QString fileName = components.takeLast();
    ScopedFd parent(openDirectoryChain(baseDirectory, components, owner, group, 0755, true));
    if (parent.get() < 0) {
        return false;
    }

    QByteArray encodedName = QFile::encodeName(fileName);
    int fd = ::openat(parent.get(), encodedName.constData(),
                      O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    bool created = false;
    if (fd < 0 && errno == ENOENT) {
        fd = ::openat(parent.get(), encodedName.constData(),
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
        created = true;
    }
    if (fd < 0) {
        return false;
    }

    ScopedFd file(fd);
    struct stat st;
    if (::fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode)
        || (!created && (st.st_uid != owner || st.st_nlink != 1))) {
        errno = EPERM;
        return false;
    }
    if (::fchown(file.get(), owner, group) != 0 || ::fchmod(file.get(), mode) != 0
        || ::ftruncate(file.get(), 0) != 0 || !writeAll(file.get(), content)
        || ::fsync(file.get()) != 0) {
        return false;
    }
    return true;
}

bool RealSystemOps::removeFileBeneath(const QString &baseDirectory,
                                      const QString &relativePath,
                                      uid_t owner)
{
    QStringList components;
    if (!relativeComponents(relativePath, &components) || components.isEmpty()) {
        return false;
    }

    QString fileName = components.takeLast();
    ScopedFd parent(openDirectoryChain(baseDirectory, components, owner, owner, 0755, false));
    if (parent.get() < 0) {
        return false;
    }

    QByteArray encodedName = QFile::encodeName(fileName);
    ScopedFd file(::openat(parent.get(), encodedName.constData(),
                           O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (file.get() < 0) {
        return errno == ENOENT;
    }

    struct stat st;
    if (::fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode)
        || st.st_uid != owner || st.st_nlink != 1) {
        errno = EPERM;
        return false;
    }

    return ::unlinkat(parent.get(), encodedName.constData(), 0) == 0;
}

bool RealSystemOps::copyFileBeneath(const QString &sourcePath, uid_t sourceOwner,
                                    const QString &baseDirectory,
                                    const QString &relativePath,
                                    uid_t owner, gid_t group, mode_t mode)
{
    QByteArray sourceName = QFile::encodeName(sourcePath);
    ScopedFd source(::open(sourceName.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (source.get() < 0) {
        return false;
    }

    struct stat st;
    constexpr off_t maxCopySize = 64 * 1024 * 1024;
    if (::fstat(source.get(), &st) != 0 || !S_ISREG(st.st_mode)
        || st.st_uid != sourceOwner || st.st_size < 0 || st.st_size > maxCopySize) {
        errno = EPERM;
        return false;
    }

    QByteArray content;
    content.resize(static_cast<qsizetype>(st.st_size));
    qsizetype offset = 0;
    while (offset < content.size()) {
        ssize_t count = ::read(source.get(), content.data() + offset,
                               static_cast<size_t>(content.size() - offset));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        offset += count;
    }

    return writeFileBeneath(baseDirectory, relativePath, content, owner, group, mode);
}

bool RealSystemOps::bindMountBeneath(const QString &sourcePath, uid_t sourceOwner,
                                     const QString &baseDirectory,
                                     const QString &relativeTarget)
{
    QStringList components;
    if (!relativeComponents(relativeTarget, &components)) {
        return false;
    }

    QByteArray sourceName = QFile::encodeName(sourcePath);
    ScopedFd source(::open(sourceName.constData(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (source.get() < 0) {
        return false;
    }

    struct stat sourceStat;
    if (::fstat(source.get(), &sourceStat) != 0 || !S_ISDIR(sourceStat.st_mode)
        || sourceStat.st_uid != sourceOwner) {
        errno = EPERM;
        return false;
    }

    struct stat baseStat;
    ScopedFd base(::open(QFile::encodeName(baseDirectory).constData(),
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (base.get() < 0 || ::fstat(base.get(), &baseStat) != 0) {
        return false;
    }

    ScopedFd target(openDirectoryChain(baseDirectory, components,
                                       baseStat.st_uid, baseStat.st_gid, 0755, true));
    if (target.get() < 0) {
        return false;
    }

    QByteArray sourceFdPath = QByteArrayLiteral("/proc/self/fd/") + QByteArray::number(source.get());
    QByteArray targetFdPath = QByteArrayLiteral("/proc/self/fd/") + QByteArray::number(target.get());
    return ::mount(sourceFdPath.constData(), targetFdPath.constData(), nullptr,
                   MS_BIND | MS_REC, nullptr) == 0;
}

bool RealSystemOps::setUserAcl(const QString &path, uid_t pathOwner, uid_t targetUid,
                               bool read, bool execute, bool remove)
{
    return setAclEntry(path, pathOwner, ACL_USER, targetUid,
                       read, false, execute, remove);
}

bool RealSystemOps::setGroupAcl(const QString &path, uid_t pathOwner, gid_t targetGid,
                                bool read, bool write, bool execute, bool remove)
{
    return setAclEntry(path, pathOwner, ACL_GROUP, targetGid,
                       read, write, execute, remove);
}

bool RealSystemOps::statPath(const QString &path, struct stat *buf)
{
    return stat(path.toLocal8Bit().constData(), buf) == 0;
}

bool RealSystemOps::isCharDevice(mode_t mode)
{
    return S_ISCHR(mode);
}

int RealSystemOps::chown(const QString &path, uid_t owner, gid_t group)
{
    return ::chown(path.toLocal8Bit().constData(), owner, group);
}

int RealSystemOps::chmod(const QString &path, mode_t mode)
{
    return ::chmod(path.toLocal8Bit().constData(), mode);
}

QProcess *RealSystemOps::createProcess(QObject *parent)
{
    return new QProcess(parent);
}

void RealSystemOps::startProcess(QProcess *process, const QString &program, const QStringList &arguments)
{
    process->start(program, arguments);
}

bool RealSystemOps::waitForFinished(QProcess *process, int msecs)
{
    return process->waitForFinished(msecs);
}

int RealSystemOps::processExitCode(QProcess *process)
{
    return process->exitCode();
}

QByteArray RealSystemOps::readStandardError(QProcess *process)
{
    return process->readAllStandardError();
}

QByteArray RealSystemOps::readAllStandardOutput(QProcess *process)
{
    return process->readAllStandardOutput();
}

QStringList RealSystemOps::entryList(const QString &path, const QStringList &nameFilters, QDir::Filters filters)
{
    QDir dir(path);
    return dir.entryList(nameFilters, filters);
}

bool RealSystemOps::killProcess(pid_t pid, int signal)
{
    return ::kill(pid, signal) == 0;
}

bool RealSystemOps::checkAuthorization(const QString &action, const QString &callerBusName)
{
    if (callerBusName.isEmpty()) {
        qWarning() << "checkAuthorization: caller bus name is empty";
        return false;
    }

#ifdef HAVE_POLKITQT
    PolkitQt1::Authority *authority = PolkitQt1::Authority::instance();
    if (authority->hasError()) {
        qWarning() << "Polkit authority error:" << authority->lastError()
                   << "- denying action:" << action;
        return false;
    }

    PolkitQt1::Authority::Result result =
        authority->checkAuthorizationSync(
            action,
            PolkitQt1::SystemBusNameSubject(callerBusName),
            PolkitQt1::Authority::AllowUserInteraction);

    if (result == PolkitQt1::Authority::Unknown) {
        qWarning() << "Polkit returned Unknown (daemon unavailable?) for action:" << action
                   << "caller:" << callerBusName;
        return false;
    }
    if (result != PolkitQt1::Authority::Yes) {
        qWarning() << "Polkit authorization denied for action:" << action
                   << "caller:" << callerBusName;
        return false;
    }
    return true;
#else
    qWarning() << "Polkit not available, denying privileged action:" << action;
    return false;
#endif
}

std::optional<uid_t> RealSystemOps::serviceUid(const QString &callerBusName)
{
    if (callerBusName.isEmpty()) {
        return std::nullopt;
    }

    QDBusConnectionInterface *interface = QDBusConnection::systemBus().interface();
    if (!interface) {
        return std::nullopt;
    }

    QDBusReply<uint> reply = interface->serviceUid(callerBusName);
    if (!reply.isValid()) {
        qWarning() << "Failed to resolve D-Bus caller UID:" << reply.error().message();
        return std::nullopt;
    }

    return static_cast<uid_t>(reply.value());
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 CouchPlay Contributors

#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>

#include <optional>

#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * SystemOps - Abstract interface for system operations
 *
 * This interface allows mocking of privileged system operations for testing.
 * RealSystemOps provides actual implementations using system calls.
 */
class SystemOps
{
public:
    virtual ~SystemOps() = default;

    // User/group lookup operations
    virtual struct passwd *getpwnam(const char *name) = 0;
    virtual struct passwd *getpwuid(uid_t uid) = 0;
    virtual struct group *getgrnam(const char *name) = 0;

    // Filesystem operations
    virtual bool fileExists(const QString &path) = 0;
    virtual bool isDirectory(const QString &path) = 0;
    virtual bool mkpath(const QString &path) = 0;
    virtual bool removeFile(const QString &path) = 0;
    virtual bool copyFile(const QString &source, const QString &dest) = 0;
    virtual bool writeFile(const QString &path, const QByteArray &content) = 0;
    virtual bool createDirectoryBeneath(const QString &baseDirectory,
                                        const QString &relativePath,
                                        uid_t owner, gid_t group, mode_t mode) = 0;
    virtual bool writeFileBeneath(const QString &baseDirectory,
                                  const QString &relativePath,
                                  const QByteArray &content,
                                  uid_t owner, gid_t group, mode_t mode) = 0;
    virtual bool removeFileBeneath(const QString &baseDirectory,
                                   const QString &relativePath,
                                   uid_t owner) = 0;
    virtual bool copyFileBeneath(const QString &sourcePath, uid_t sourceOwner,
                                 const QString &baseDirectory,
                                 const QString &relativePath,
                                 uid_t owner, gid_t group, mode_t mode) = 0;
    virtual bool bindMountBeneath(const QString &sourcePath, uid_t sourceOwner,
                                  const QString &baseDirectory,
                                  const QString &relativeTarget) = 0;
    virtual bool setUserAcl(const QString &path, uid_t pathOwner, uid_t targetUid,
                            bool read, bool execute, bool remove) = 0;
    virtual bool setGroupAcl(const QString &path, uid_t pathOwner, gid_t targetGid,
                             bool read, bool write, bool execute, bool remove) = 0;

    // Device path validation
    virtual bool statPath(const QString &path, struct stat *buf) = 0;
    virtual bool isCharDevice(mode_t mode) = 0;

    // Ownership and permissions
    virtual int chown(const QString &path, uid_t owner, gid_t group) = 0;
    virtual int chmod(const QString &path, mode_t mode) = 0;

    // Process operations
    virtual QProcess *createProcess(QObject *parent = nullptr) = 0;
    virtual void startProcess(QProcess *process, const QString &program, const QStringList &arguments) = 0;
    virtual bool waitForFinished(QProcess *process, int msecs) = 0;
    virtual int processExitCode(QProcess *process) = 0;
    virtual QByteArray readStandardError(QProcess *process) = 0;
    virtual QByteArray readAllStandardOutput(QProcess *process) = 0;

    // Directory listing
    virtual QStringList entryList(const QString &path, const QStringList &nameFilters, QDir::Filters filters) = 0;

    // Process signaling
    virtual bool killProcess(pid_t pid, int signal) = 0;

    // Authorization check
    virtual bool checkAuthorization(const QString &action, const QString &callerBusName) = 0;
    virtual std::optional<uid_t> serviceUid(const QString &callerBusName) = 0;
};

/**
 * RealSystemOps - Production implementation using actual system calls
 */
class RealSystemOps : public QObject, public SystemOps
{
    Q_OBJECT

public:
    explicit RealSystemOps(QObject *parent = nullptr);

    struct passwd *getpwnam(const char *name) override;
    struct passwd *getpwuid(uid_t uid) override;
    struct group *getgrnam(const char *name) override;

    bool fileExists(const QString &path) override;
    bool isDirectory(const QString &path) override;
    bool mkpath(const QString &path) override;
    bool removeFile(const QString &path) override;
    bool copyFile(const QString &source, const QString &dest) override;
    bool writeFile(const QString &path, const QByteArray &content) override;
    bool createDirectoryBeneath(const QString &baseDirectory,
                                const QString &relativePath,
                                uid_t owner, gid_t group, mode_t mode) override;
    bool writeFileBeneath(const QString &baseDirectory,
                          const QString &relativePath,
                          const QByteArray &content,
                          uid_t owner, gid_t group, mode_t mode) override;
    bool removeFileBeneath(const QString &baseDirectory,
                           const QString &relativePath,
                           uid_t owner) override;
    bool copyFileBeneath(const QString &sourcePath, uid_t sourceOwner,
                         const QString &baseDirectory,
                         const QString &relativePath,
                         uid_t owner, gid_t group, mode_t mode) override;
    bool bindMountBeneath(const QString &sourcePath, uid_t sourceOwner,
                          const QString &baseDirectory,
                          const QString &relativeTarget) override;
    bool setUserAcl(const QString &path, uid_t pathOwner, uid_t targetUid,
                    bool read, bool execute, bool remove) override;
    bool setGroupAcl(const QString &path, uid_t pathOwner, gid_t targetGid,
                     bool read, bool write, bool execute, bool remove) override;

    bool statPath(const QString &path, struct stat *buf) override;
    bool isCharDevice(mode_t mode) override;

    int chown(const QString &path, uid_t owner, gid_t group) override;
    int chmod(const QString &path, mode_t mode) override;

    QProcess *createProcess(QObject *parent = nullptr) override;
    void startProcess(QProcess *process, const QString &program, const QStringList &arguments) override;
    bool waitForFinished(QProcess *process, int msecs) override;
    int processExitCode(QProcess *process) override;
    QByteArray readStandardError(QProcess *process) override;
    QByteArray readAllStandardOutput(QProcess *process) override;

    QStringList entryList(const QString &path, const QStringList &nameFilters, QDir::Filters filters) override;

    bool killProcess(pid_t pid, int signal) override;

    bool checkAuthorization(const QString &action, const QString &callerBusName) override;
    std::optional<uid_t> serviceUid(const QString &callerBusName) override;
};

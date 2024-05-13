#include "sd.h"
#include "scall.h"

int (*sceFsUfsAllocateSaveData)(int fd, uint64_t imageSize, uint64_t imageFlags, int ext);
int (*sceFsInitCreatePfsSaveDataOpt)(CreatePfsSaveDataOpt *opt);
int (*sceFsCreatePfsSaveDataImage)(CreatePfsSaveDataOpt *opt, const char *volumePath, int idk, uint64_t volumeSize, uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN]);
int (*sceFsInitMountSaveDataOpt)(MountSaveDataOpt *opt);
int (*sceFsMountSaveData)(MountSaveDataOpt *opt, const char *volumePath, const char *mountPath, uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN]);
int (*sceFsInitUmountSaveDataOpt)(UmountSaveDataOpt *opt);
int (*sceFsUmountSaveData)(UmountSaveDataOpt *opt, const char *mountPath, int handle, bool ignoreErrors);
void (*statfs)();

int loadPrivLibs() {
    const char privDir[] = "/system/priv/lib";
    const char commonDir[] = "/system/common/lib";
    int sys;
    int kernel_sys;
    int ret = 0;

    if (jbc_mount_in_sandbox(privDir, "priv") != 0) {
        sceKernelDebugOutText(0, "Failed to mount system/priv/lib directory\n");
        return -1;
    }

    sys = sceKernelLoadStartModule("/priv/libSceFsInternalForVsh.sprx", 0, NULL, 0, NULL, NULL);
    jbc_unmount_in_sandbox("priv");

    if (sys >= 0) {
        sceKernelDlsym(sys, "sceFsInitCreatePfsSaveDataOpt",    (void **)&sceFsInitCreatePfsSaveDataOpt);
        sceKernelDlsym(sys, "sceFsCreatePfsSaveDataImage",      (void **)&sceFsCreatePfsSaveDataImage);
        sceKernelDlsym(sys, "sceFsUfsAllocateSaveData",         (void **)&sceFsUfsAllocateSaveData);
        sceKernelDlsym(sys, "sceFsInitMountSaveDataOpt",        (void **)&sceFsInitMountSaveDataOpt);
        sceKernelDlsym(sys, "sceFsMountSaveData",               (void **)&sceFsMountSaveData);
        sceKernelDlsym(sys, "sceFsInitUmountSaveDataOpt",       (void **)&sceFsInitUmountSaveDataOpt);
        sceKernelDlsym(sys, "sceFsUmountSaveData",              (void **)&sceFsUmountSaveData);
    }
    else {
        sceKernelDebugOutText(0, "sys < 0\n");
        ret = -1;
    }

    if (jbc_mount_in_sandbox(commonDir, "common") != 0) {
        sceKernelDebugOutText(0, "Failed to mount /system/common/lib directory\n");
        ret = -1;
    }
    kernel_sys = sceKernelLoadStartModule("/common/libkernel_sys.sprx", 0, NULL, 0, NULL, NULL);
    jbc_unmount_in_sandbox("common");

    if (kernel_sys >= 0) {
        sceKernelDlsym(kernel_sys, "statfs", (void **)&statfs);
    }
    else {
        sceKernelDebugOutText(0, "kernel_sys < 0\n");
        ret = -1;
    }

    return ret;
}

int generateSealedKey(uint8_t data[ENC_SEALEDKEY_LEN]) {
    uint8_t dummy[0x30];
    uint8_t sealedKey[ENC_SEALEDKEY_LEN];
    int fd;

    UNUSED(dummy);

    memset(sealedKey, 0, sizeof(sealedKey));

    if ((fd = open("/dev/sbl_srv", 0, O_RDWR)) == -1) {
        return -1;
    }

    if (ioctl(fd, 0x40845303, sealedKey) == -1) {
        close(fd);
        return -1;
    }

    memcpy(data, sealedKey, sizeof(sealedKey));
    close(fd);

    return 0;
}

int decryptSealedKey(uint8_t enc_key[ENC_SEALEDKEY_LEN], uint8_t dec_key[DEC_SEALEDKEY_LEN]) {
    uint8_t dummy[0x10];
    int fd;
    uint8_t data[ENC_SEALEDKEY_LEN + DEC_SEALEDKEY_LEN];
    memset(data, 0, sizeof(data));

    UNUSED(dummy);

    if ((fd = open("/dev/sbl_srv", 0, O_RDWR)) == -1) {
        return -1;
    }

    memcpy(data, enc_key, ENC_SEALEDKEY_LEN);

    if (ioctl(fd, 0xc0845302, data) == -1) {
        close(fd);
        return -1;
    }

    memcpy(dec_key, &data[ENC_SEALEDKEY_LEN], DEC_SEALEDKEY_LEN);

    close(fd);
    return 0;
}

int decryptSealedKeyAtPath(const char *keyPath, uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN]) {
    uint8_t sealedKey[ENC_SEALEDKEY_LEN];
    int fd;
    ssize_t bytesRead;

    if ((fd = sys_open(keyPath, O_RDONLY, 0)) == -1) {
        return -1;
    }

    if (read(fd, sealedKey, ENC_SEALEDKEY_LEN) != ENC_SEALEDKEY_LEN) {
        return -2;
        close(fd);
    }
    close(fd);

    if (decryptSealedKey(sealedKey, decryptedSealedKey) == -1) {
        return -3;
    }
    return 0;
}

int mountSave(const char *folder, const char *saveName, const char *mountPath) {
    char volumeKeyPath[MAX_PATH_LEN];
    char volumePath[MAX_PATH_LEN];
    uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN];
    int ret;
    MountSaveDataOpt opt;
    char bid[] = "system";

    sprintf(volumeKeyPath, "%s/%s.bin", folder, saveName);
    sprintf(volumePath, "%s/%s", folder, saveName);

    if ((ret = decryptSealedKeyAtPath(volumeKeyPath, decryptedSealedKey)) < 0) {
        return ret;
    }

    sceFsInitMountSaveDataOpt(&opt);
    opt.budgetid = bid;

    if ((ret = sceFsMountSaveData(&opt, volumePath, mountPath, decryptedSealedKey)) < 0) {
        return ret;
    }

    return 0;
}

int umountSave(const char *mountPath, int handle, bool ignoreErrors) {
    UmountSaveDataOpt opt;
    sceFsInitUmountSaveDataOpt(&opt);
    return sceFsUmountSaveData(&opt, mountPath, handle, ignoreErrors);
}

uint16_t maxKeyset;
uint16_t getMaxKeySet() {
    if (maxKeyset > 0) {
        return maxKeyset;
    }

    uint8_t sampleSealedKey[ENC_SEALEDKEY_LEN];
    if (generateSealedKey(sampleSealedKey) != 0) {
        return 0;
    }

    maxKeyset = (sampleSealedKey[9] << 8) + sampleSealedKey[8];
    return maxKeyset;
}
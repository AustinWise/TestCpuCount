// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/*++

Module Name:

    cgroup.cpp

Abstract:
    Read memory and cpu limits for the current process
--*/

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits>
#include <limits.h>
#include <stdio.h>
#include <sys/resource.h>
#include <algorithm>
#include <sys/vfs.h>
#include <stdio.h>

#define UINT uint32_t

#define CGROUP2_SUPER_MAGIC 0x63677270
#define TMPFS_MAGIC 0x01021994

#define BASE_TEN 10

#define PROC_MOUNTINFO_FILENAME "/proc/self/mountinfo"
#define PROC_CGROUP_FILENAME "/proc/self/cgroup"
#define PROC_STATM_FILENAME "/proc/self/statm"
#define CGROUP1_CFS_QUOTA_FILENAME "/cpu.cfs_quota_us"
#define CGROUP1_CFS_PERIOD_FILENAME "/cpu.cfs_period_us"
#define CGROUP2_CPU_MAX_FILENAME "/cpu.max"


class CGroup
{
    // the cgroup version number or 0 to indicate cgroups are not found or not enabled
    static int s_cgroup_version;

    static char *s_cpu_cgroup_path;

public:
    static void Initialize()
    {
        s_cgroup_version = FindCGroupVersion();
        printf("s_cgroup_version = %d\n", CGroup::s_cgroup_version);
        FindCGroupPath(s_cgroup_version == 1 ? &IsCGroup1CpuSubsystem : nullptr, &s_cpu_cgroup_path);
        printf("s_cpu_cgroup_path = %s\n", s_cpu_cgroup_path);
    }

    static void Cleanup()
    {
        free(s_cpu_cgroup_path);
    }

    static bool GetCpuLimit(UINT *val)
    {
        if (s_cgroup_version == 0)
        {
            puts("no cgroup version.");
            return false;
        }
        else if (s_cgroup_version == 1)
            return GetCGroup1CpuLimit(val);
        else if (s_cgroup_version == 2)
            return GetCGroup2CpuLimit(val);
        else
        {
            puts("Unknown cgroup version.");
            return false;
        }
    }

private:
    static int FindCGroupVersion()
    {
        // It is possible to have both cgroup v1 and v2 enabled on a system.
        // Most non-bleeding-edge Linux distributions fall in this group. We
        // look at the file system type of /sys/fs/cgroup to determine which
        // one is the default. For more details, see:
        // https://systemd.io/CGROUP_DELEGATION/#three-different-tree-setups-
        // We dont care about the difference between the "legacy" and "hybrid"
        // modes because both of those involve cgroup v1 controllers managing
        // resources.

        struct statfs stats;
        int result = statfs("/sys/fs/cgroup", &stats);

        if (result != 0)
            return 0;

        switch (stats.f_type)
        {
            case TMPFS_MAGIC: return 1;
            case CGROUP2_SUPER_MAGIC: return 2;
            default:
                return 0;
        }
    }

    static bool IsCGroup1CpuSubsystem(const char *strTok){
        return strcmp("cpu", strTok) == 0;
    }

    static void FindCGroupPath(bool (*is_subsystem)(const char *), char** pcgroup_path, char ** pcgroup_hierarchy_mount = nullptr){
        char *cgroup_path = nullptr;
        char *hierarchy_mount = nullptr;
        char *hierarchy_root = nullptr;
        char *cgroup_path_relative_to_mount = nullptr;
        size_t len;
        size_t common_path_prefix_len;

        FindHierarchyMount(is_subsystem, &hierarchy_mount, &hierarchy_root);
        if (hierarchy_mount == nullptr || hierarchy_root == nullptr)
        {
            puts("failed to FindHierarchyMount");
            goto done;
        }

        printf("hierarchy_mount: %s hierarchy_root: %s\n", hierarchy_mount, hierarchy_root);

        cgroup_path_relative_to_mount = FindCGroupPathForSubsystem(is_subsystem);
        if (cgroup_path_relative_to_mount == nullptr)
        {
            puts("failed to FindHierarchyMount");
            goto done;
        }

        printf("cgroup_path_relative_to_mount: %s\n", cgroup_path_relative_to_mount);

        len = strlen(hierarchy_mount);
        len += strlen(cgroup_path_relative_to_mount);
        cgroup_path = (char*)malloc(len+1);
        if (cgroup_path == nullptr)
           goto done;

        strcpy(cgroup_path, hierarchy_mount);
        // For a host cgroup, we need to append the relative path.
        // The root and cgroup path can share a common prefix of the path that should not be appended.
        // Example 1 (docker):
        // hierarchy_mount:               /sys/fs/cgroup/cpu
        // hierarchy_root:                /docker/87ee2de57e51bc75175a4d2e81b71d162811b179d549d6601ed70b58cad83578
        // cgroup_path_relative_to_mount: /docker/87ee2de57e51bc75175a4d2e81b71d162811b179d549d6601ed70b58cad83578/my_named_cgroup
        // append do the cgroup_path:     /my_named_cgroup
        // final cgroup_path:             /sys/fs/cgroup/cpu/my_named_cgroup
        //
        // Example 2 (out of docker)
        // hierarchy_mount:               /sys/fs/cgroup/cpu
        // hierarchy_root:                /
        // cgroup_path_relative_to_mount: /my_named_cgroup
        // append do the cgroup_path:     /my_named_cgroup
        // final cgroup_path:             /sys/fs/cgroup/cpu/my_named_cgroup
        common_path_prefix_len = strlen(hierarchy_root);
        if ((common_path_prefix_len == 1) || strncmp(hierarchy_root, cgroup_path_relative_to_mount, common_path_prefix_len) != 0)
        {
            common_path_prefix_len = 0;
        }

        assert((cgroup_path_relative_to_mount[common_path_prefix_len] == '/') || (cgroup_path_relative_to_mount[common_path_prefix_len] == '\0'));

        strcat(cgroup_path, cgroup_path_relative_to_mount + common_path_prefix_len);

    done:
        free(hierarchy_root);
        free(cgroup_path_relative_to_mount);
        *pcgroup_path = cgroup_path;
        if (pcgroup_hierarchy_mount != nullptr)
        {
            *pcgroup_hierarchy_mount = hierarchy_mount;
        }
        else
        {
            free(hierarchy_mount);
        }
    }

    static void FindHierarchyMount(bool (*is_subsystem)(const char *), char** pmountpath, char** pmountroot)
    {
        char *line = nullptr;
        size_t lineLen = 0;
        char *filesystemType = nullptr;
        char *options = nullptr;
        char *mountpath = nullptr;
        char *mountroot = nullptr;

        FILE *mountinfofile = fopen(PROC_MOUNTINFO_FILENAME, "r");
        if (mountinfofile == nullptr)
            goto done;

        while (getline(&line, &lineLen, mountinfofile) != -1)
        {
            char* separatorChar = strstr(line, " - ");;

            // See man page of proc to get format for /proc/self/mountinfo file
            int sscanfRet = sscanf(separatorChar,
                                     " - %ms %*s %ms",
                                     &filesystemType,
                                     &options);
            if (sscanfRet != 2)
            {
                puts("Failed to parse mount info file contents with sscanf.");
                exit(1);
            }

            if (strncmp(filesystemType, "cgroup", 6) == 0)
            {
                bool isSubsystemMatch = is_subsystem == nullptr;
                if (!isSubsystemMatch)
                {
                    char* context = nullptr;
                    char* strTok = strtok_r(options, ",", &context);
                    while (!isSubsystemMatch && strTok != nullptr)
                    {
                        isSubsystemMatch = is_subsystem(strTok);
                        strTok = strtok_r(nullptr, ",", &context);
                    }
                }
                if (isSubsystemMatch)
                {
                    sscanfRet = sscanf(line,
                                        "%*s %*s %*s %ms %ms ",
                                        &mountroot,
                                        &mountpath);
                    if (sscanfRet != 2)
                    {
                        puts("Failed to parse mount info file contents with sscanf.");
                        exit(1);
                    }

                    // assign the output arguments and clear the locals so we don't free them.
                    *pmountpath = mountpath;
                    *pmountroot = mountroot;
                }
            }
        }
    done:
        free(line);
        if (mountinfofile)
            fclose(mountinfofile);
    }

    static char* FindCGroupPathForSubsystem(bool (*is_subsystem)(const char *))
    {
        char *line = nullptr;
        size_t lineLen = 0;
        size_t maxLineLen = 0;
        char *subsystem_list = nullptr;
        char *cgroup_path = nullptr;
        bool result = false;

        FILE *cgroupfile = fopen(PROC_CGROUP_FILENAME, "r");
        if (cgroupfile == nullptr)
            goto done;

        while (!result && getline(&line, &lineLen, cgroupfile) != -1)
        {
            if (s_cgroup_version == 1)
            {
                // See man page of proc to get format for /proc/self/cgroup file
                int sscanfRet = sscanf(line,
                                         "%*[^:]:%m[^:]:%ms",
                                         &subsystem_list,
                                         &cgroup_path);
                if (sscanfRet != 2)
                {
                    puts("Failed to parse cgroup info file contents with sscanf.");
                    exit(1);
                }

                char* context = nullptr;
                char* strTok = strtok_r(subsystem_list, ",", &context);
                while (strTok != nullptr)
                {
                    if (is_subsystem(strTok))
                    {
                        result = true;
                        break;
                    }
                    strTok = strtok_r(nullptr, ",", &context);
                }
            }
            else if (s_cgroup_version == 2)
            {
                // See https://www.kernel.org/doc/Documentation/cgroup-v2.txt
                // Look for a "0::/some/path"
                int sscanfRet = sscanf(line,
                                         "0::%ms",
                                         &cgroup_path);
                if (sscanfRet == 1)
                {
                    result = true;
                }
            }
            else
            {
                puts("Unknown cgroup version in mountinfo.");
                exit(1);
            }
        }
    done:
        free(line);
        if (cgroupfile)
            fclose(cgroupfile);
        return cgroup_path;
    }

    static bool GetCGroup1CpuLimit(UINT *val)
    {
        long long quota;
        long long period;

        quota = ReadCpuCGroupValue(CGROUP1_CFS_QUOTA_FILENAME);
        if (quota <= 0)
        {
            puts("ReadCpuCGroupValue(CGROUP1_CFS_QUOTA_FILENAME) failed");
            return false;
        }

        period = ReadCpuCGroupValue(CGROUP1_CFS_PERIOD_FILENAME);
        if (period <= 0)
        {
            puts("ReadCpuCGroupValue(CGROUP1_CFS_PERIOD_FILENAME) failed");
            return false;
        }

        ComputeCpuLimit(period, quota, val);

        return true;
    }

    static bool GetCGroup2CpuLimit(UINT *val)
    {
        char *filename = nullptr;
        FILE *file = nullptr;
        char *endptr = nullptr;
        char *max_quota_string = nullptr;
        char *period_string = nullptr;
        char *context = nullptr;
        char *line = nullptr;
        size_t lineLen = 0;

        long long quota = 0;
        long long period = 0;

        bool result = false;

        if (s_cpu_cgroup_path == nullptr)
            return false;

        if (asprintf(&filename, "%s%s", s_cpu_cgroup_path, CGROUP2_CPU_MAX_FILENAME) < 0)
            return false;

        file = fopen(filename, "r");
        if (file == nullptr)
        {
            printf("GetCGroup2CpuLimit: failed to open %s\n", filename);
            goto done;
        }

        if (getline(&line, &lineLen, file) == -1)
        {
            printf("GetCGroup2CpuLimit: failed read line in %s\n", filename);
            goto done;
        }

        // The expected format is:
        //     $MAX $PERIOD
        // Where "$MAX" may be the string literal "max"

        max_quota_string = strtok_r(line, " ", &context);
        if (max_quota_string == nullptr)
        {
            puts("Unable to parse " CGROUP2_CPU_MAX_FILENAME " file contents.");
            goto done;
        }
        period_string = strtok_r(nullptr, " ", &context);
        if (period_string == nullptr)
        {
            puts("Unable to parse " CGROUP2_CPU_MAX_FILENAME " file contents.");
            goto done;
        }

        // "max" means no cpu limit
        if (strcmp("max", max_quota_string) == 0)
        {
            puts("max CPU limit");
            goto done;
        }

        errno = 0;
        quota = strtoll(max_quota_string, &endptr, BASE_TEN);
        if (max_quota_string == endptr || errno != 0)
        {
            printf("GetCGroup2CpuLimit: failed to parse quota: %s\n", max_quota_string);
            goto done;
        }

        period = strtoll(period_string, &endptr, BASE_TEN);
        if (period_string == endptr || errno != 0)
        {
            printf("GetCGroup2CpuLimit: failed to parse period_string: %s\n", period_string);
            goto done;
        }

        ComputeCpuLimit(period, quota, val);
        result = true;

    done:
        if (file)
            fclose(file);
        free(filename);
        free(line);

        return result;
    }

    static void ComputeCpuLimit(long long period, long long quota, uint32_t *val)
    {
        printf("ComputeCpuLimit: %lld %lld\n", period, quota);
        // Cannot have less than 1 CPU
        if (quota <= period)
        {
            puts("rounding up to one CPU");
            *val = 1;
            return;
        }

        // Calculate cpu count based on quota and round it up
        double cpu_count = (double) quota / period  + 0.999999999;
        *val = (cpu_count < UINT32_MAX) ? (uint32_t)cpu_count : UINT32_MAX;
    }

    static long long ReadCpuCGroupValue(const char* subsystemFilename){
        char *filename = nullptr;
        bool result = false;
        long long val;

        if (s_cpu_cgroup_path == nullptr)
            return -1;

        if (asprintf(&filename, "%s%s", s_cpu_cgroup_path, subsystemFilename) < 0)
            return -1;

        result = ReadLongLongValueFromFile(filename, &val);
        free(filename);
        if (!result)
            return -1;

        return val;
    }

    static bool ReadLongLongValueFromFile(const char* filename, long long* val)
    {
        bool result = false;
        char *line = nullptr;
        size_t lineLen = 0;
        char *endptr = nullptr;

        if (val == nullptr)
            return false;

        FILE* file = fopen(filename, "r");
        if (file == nullptr)
        {
            printf("failed to open file %s\n", filename);
            goto done;
        }

        if (getline(&line, &lineLen, file) == -1)
            goto done;

        errno = 0;
        *val = strtoll(line, &endptr, BASE_TEN);
        if (line == endptr || errno != 0)
            goto done;

        result = true;
    done:
        if (file)
            fclose(file);
        free(line);
        return result;
    }
};

int CGroup::s_cgroup_version = 0;
char *CGroup::s_cpu_cgroup_path = nullptr;


int main(int argc, char** argv)
{
    puts("starting");
    CGroup::Initialize();
    puts("Initialized");

    uint32_t val = 0;
    if (CGroup::GetCpuLimit(&val))
    {
        printf("CGroup::GetCpuLimit: %d\n", (int)val);
    }
    else
    {
        puts("GetCpuLimit failed");
    }

    CGroup::Cleanup();
}

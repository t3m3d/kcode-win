// kbackend/build.k — streaming process executor.
//
// kr_exec() in kcc_seed.c is blocking + capped at 8 KB. For an IDE
// backend we want incremental stdout streaming so the user sees gcc
// errors / program output as they happen. cfunc { } block below
// implements popen + line-by-line forward via a callback supplied by
// the caller as a function pointer.
//
// Public API:
//   bExecStream(cmd, sinkPtr)
//     Spawns `cmd` in a shell, reads its stdout in 4 KB chunks, and
//     for each chunk calls sinkPtr(chunk_str). Returns the child's
//     exit code as a string.

module build

jxt {
    func krBuildExec(cmd, sinkPtr)
}

cfunc {
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    // Sink signature: void sink(char* chunk).
    // The chunk buffer is owned by us — sink should copy if it needs
    // to retain it past the call.
    typedef void (*kr_build_sink_t)(char*);

    char* krBuildExec(char* cmd, char* sinkPtr) {
        if (!cmd) return (char*)"-1";
        kr_build_sink_t sink = (kr_build_sink_t)sinkPtr;

#ifdef _WIN32
        FILE* p = _popen(cmd, "r");
#else
        FILE* p = popen(cmd, "r");
#endif
        if (!p) return (char*)"-1";

        char buf[4096];
        for (;;) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, p);
            if (n == 0) break;
            buf[n] = 0;
            if (sink) sink(buf);
        }

#ifdef _WIN32
        int code = _pclose(p);
#else
        int code = pclose(p);
#endif
        // _pclose returns the raw exit code on Windows; on POSIX it's
        // wait status — strip the lower 8 bits via WEXITSTATUS-like
        // shift. For simplicity we emit the raw value as a decimal
        // string; callers can sniff for nonzero == failed.
        static char out[32];
        snprintf(out, sizeof(out), "%d", code);
        return out;
    }
}

// Krypton wrapper.
func bExecStream(cmd, sinkPtr) {
    emit krBuildExec(cmd, sinkPtr)
}

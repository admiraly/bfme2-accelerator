#ifndef BFME2_FEATURES_H
#define BFME2_FEATURES_H
// Verified BFME II bindings are enabled without environment configuration.
// Explicit 0 (or another unsupported value) opts an individual feature out.
static bool bfme2FeatureEnabled(const char* name) {
    char value[8] = {};
    DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return !length || (length == 1 && value[0] == '1');
}
#endif

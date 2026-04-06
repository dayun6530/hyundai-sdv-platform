#include "LifeCycleManager/Feature.h"

#ifndef SHADOW_NAME
#error "SHADOW_NAME must be defined (e.g., NavigationFeatureShadow)"
#endif

#ifndef PRIMARY_NAME
#error "PRIMARY_NAME must be defined (e.g., NavigationFeature)"
#endif

// Thin entry for *Shadow executables only. Real logic: Feature.cpp::runShadowReceiverMain.
int main(int argc, char* argv[]) {
    return sdv::feature::runShadowReceiverMain(argc, argv, SHADOW_NAME, PRIMARY_NAME);
}

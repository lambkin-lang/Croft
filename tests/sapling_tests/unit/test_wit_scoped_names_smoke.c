#include "tests/generated/scoped_names.h"

int main(void)
{
    SapWitScopedNamesAlphaConfig alpha = {0};
    SapWitScopedNamesBetaConfig beta = {0};

    alpha.is_status_ok = 1;
    beta.is_status_ok = 0;

    (void)alpha;
    (void)beta;
    return 0;
}

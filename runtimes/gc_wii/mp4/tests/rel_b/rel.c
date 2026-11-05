/* Synthetic REL B: the same external names as REL A, different values, no ObjectSetup. */
int counter = 50;
static int scratch;

int shared_name(void) { return ++counter * 100 + scratch++; }

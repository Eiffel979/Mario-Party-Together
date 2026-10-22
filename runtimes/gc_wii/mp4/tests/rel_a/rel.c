/* Synthetic REL A for test_rel_isolation.c. REL B defines the same names. */
int counter = 5; /* .data */
static int scratch; /* .bss: zero on every link */
int setup_calls;

int shared_name(void) { return ++counter * 100 + scratch++; }
void ObjectSetup(void) { ++setup_calls; }

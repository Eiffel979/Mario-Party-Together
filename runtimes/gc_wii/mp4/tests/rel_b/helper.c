/* A second object in REL B: references between a REL's own objects resolve inside the REL. */
extern int counter;
int counter_plus(int n) { return counter + n; }

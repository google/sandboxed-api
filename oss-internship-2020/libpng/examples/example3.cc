int main(void) {
   double err = 0;
   unsigned int xerr = 0;
   unsigned int r = 32769;
   {
      unsigned int x = 0;

      do {
         unsigned int t = x + (x >> 16) /*+ (x >> 31)*/ + r;
         double v = x, errtest;

         if (t < x) {
            fprintf(stderr, "overflow: %u+%u -> %u\n", x, r, t);
            return 1;
         }

         v /= 65535;
         errtest = v;
         t >>= 16;
         errtest -= t;

         if (errtest > err) {
            err = errtest;
            xerr = x;

            if (errtest >= .5) {
               fprintf(stderr, "error: %u/65535 = %f, not %u, error %f\n",
                     x, v, t, errtest);
               return 0;
            }
         }
      } while (++x <= 65535U*65535U);
   }

   printf("error %f @ %u\n", err, xerr);

   return 0;
}
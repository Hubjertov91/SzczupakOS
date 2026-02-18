#include <math.h>

long power(long base, long exp) {
    long result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

long factorial(long n) {
    if (n <= 1) return 1;
    long result = 1;
    for (long i = 2; i <= n; i++) result *= i;
    return result;
}

long gcd(long a, long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        long tmp = b;
        b = a % b;
        a = tmp;
    }
    return a;
}

long lcm(long a, long b) {
    if (a == 0 || b == 0) return 0;
    return (a / gcd(a, b)) * b;
}

int is_prime(long n) {
    if (n < 2) return 0;
    if (n < 4) return 1;
    if (n % 2 == 0 || n % 3 == 0) return 0;
    for (long i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    }
    return 1;
}
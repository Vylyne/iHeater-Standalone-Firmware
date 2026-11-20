#include <math.h>
#include <unity.h>

#include "thermistor.h"

/* Reference: the same Steinhart-Hart fit and conversion, all in double. */
typedef struct
{
    double c1, c2, c3;
} RefCoeffs;

static RefCoeffs ref_fit(const ThermistorData *d)
{
    const double k = 273.15;
    double inv_t1 = 1.0 / (d->t1 + k), inv_t2 = 1.0 / (d->t2 + k), inv_t3 = 1.0 / (d->t3 + k);
    double l1 = log(d->r1), l2 = log(d->r2), l3 = log(d->r3);
    double c1_ = l1 * l1 * l1, c2_ = l2 * l2 * l2, c3_ = l3 * l3 * l3;
    double it12 = inv_t1 - inv_t2, it13 = inv_t1 - inv_t3;
    double l12 = l1 - l2, l13 = l1 - l3;
    double c12 = c1_ - c2_, c13 = c1_ - c3_;
    RefCoeffs c;
    c.c3 = (it12 - it13 * l12 / l13) / (c12 - c13 * l12 / l13);
    c.c2 = (it12 - c.c3 * c12) / l12;
    c.c1 = inv_t1 - c.c2 * l1 - c.c3 * c1_;
    return c;
}

static double ref_temp(const RefCoeffs *c, double pullup, double adc)
{
    double r = pullup * adc / (1.0 - adc);
    double l = log(r);
    return 1.0 / (c->c1 + c->c2 * l + c->c3 * l * l * l) - 273.15;
}

void setUp(void) {}
void tearDown(void) {}

/* Firmware maths (float) stays within 0.05 °C of the double reference
 * across the working range, for every thermistor in the table. */
static void test_float_matches_double_reference(void)
{
    const double pullup = 4700.0;
    const ThermistorData *d;
    for (uint8_t type = 0; (d = get_thermistor_data(type)) != NULL; type++)
    {
        Thermistor t;
        thermistor_init(&t, (float)pullup, 0.0f);
        thermistor_set_coefficients(&t, d->t1, d->r1, d->t2, d->r2, d->t3, d->r3);
        RefCoeffs rc = ref_fit(d);

        int checked = 0;
        for (int i = 1; i < 2000; i++)
        {
            double adc = i / 2000.0;
            double want = ref_temp(&rc, pullup, adc);
            if (want < -20.0 || want > 150.0)
            {
                continue;
            }
            float got = thermistor_calc_temp(&t, (float)adc);
            TEST_ASSERT_FLOAT_WITHIN(0.05f, (float)want, got);
            checked++;
        }
        TEST_ASSERT_GREATER_THAN(100, checked);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_float_matches_double_reference);
    return UNITY_END();
}

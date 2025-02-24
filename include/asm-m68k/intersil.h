#ifndef _SUN3_INTERSIL_H
#define _SUN3_INTERSIL_H
/* bits 0 and 1 */
#define INTERSIL_FREQ_32K        0x00
#define INTERSIL_FREQ_1M         0x01
#define INTERSIL_FREQ_2M         0x02
#define INTERSIL_FREQ_4M         0x03

/* bit 2 */
#define INTERSIL_12H_MODE   0x00
#define INTERSIL_24H_MODE   0x04

/* bit 3 */
#define INTESIL_STOP            0x00
#define INTERSIL_RUN             0x08

/* bit 4 */
#define INTERSIL_INT_ENABLE     0x10
#define INTERSIL_INT_DISABLE    0x00
                
/* bit 5 */
#define INTERSIL_MODE_NORMAL     0x00
#define INTERSIL_MODE_TEST       0x20

#define INTERSIL_HZ_100_MASK	 0x02

struct intersil_dt {
	u_char	csec;
	u_char	hour;
	u_char	minute;
	u_char	second;
	u_char	month;
	u_char	day;
	u_char	year;
	u_char	weekday;
};

struct intersil_7170 {
	struct intersil_dt counter;
	struct intersil_dt alarm;
	u_char	int_reg;
	u_char	cmd_reg;
};

extern volatile char* clock_va;
#define intersil_clock ((volatile struct intersil_7170 *) clock_va)
#define intersil_clear() (void)intersil_clock->int_reg
#endif

/* bitflags on the map field */
#define PLAYER		3
#define DISABLED	(1 << 3)
#define VISITED		(1 << 4)
#define FROM_UP		(0 << 5)
#define FROM_BOT	(1 << 5)
#define FROM_LEFT	(2 << 5)
#define FROM_RIGHT	(3 << 5)

void process_map(char *map, int start_y, int start_x);


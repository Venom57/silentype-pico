/*
 * Silendump
 * Dumps the Silentype font
 *
 * (C) 2010 by Marc S. Ressl
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
	FILE *fp;
	char buf[0x60*5];
	int i;

	if (argc != 2)
	{
		printf("Usage: silendump [file]\n");
		return 1;
	}

	fp = fopen(argv[1], "rb");
	if (!fp)
	{
		printf("could not open file.\n");
		return 2;
	}

	fseek(fp, 0x614, SEEK_SET);
	fread(buf, 1, 0x60*5, fp);
	fclose(fp);

	for (i = 0; i < 0x60; i++)
	{
		int y;

		putchar(i + 0x20);
		putchar('\n');

		for (y = 0; y < 7; y++)
		{
			int x;
			for (x = 0; x < 5; x++)
			{
				int p = x * 0x60 + i;
				int d = buf[p];

				if (d & (1 << (6 - y)))
					putchar('*');
				else
					putchar(' ');
			}
			putchar('\n');
		}
	}

	return 0;
}

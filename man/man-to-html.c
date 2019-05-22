// Usage:
// troff -man -Tascii gxemul.1 | grotty -c | ./man-to-html > gxemul.1.html

#include <stdio.h>

int main()
{
	char lastc = 0;

	printf("<pre>");

	while (!feof(stdin))
	{
		char c = fgetc(stdin);

		if (c == '\b') {
			char c2 = fgetc(stdin);
			if (lastc == '_')
				printf("<i>%c</i>", c2);
			else
				printf("<b>%c</b>", c2);

			c = fgetc(stdin);
		} else if (lastc != 0) {
			printf("%c", lastc);
		}

		lastc = c;
	}

	printf("</pre>");

	return 0;
}


/*
 * Copyright (c) Qualcomm Technologies, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * Patched in place by CMakeLists.txt after the final link, with the
 * address main() was linked at. A self-relocating PIE image only knows
 * where it actually ends up executing at runtime, so this is the only
 * way to recover the link-time address for comparison.
 */
volatile uint8_t main_link_addr_sentinel[8] = {
	0xef, 0xbe, 0xad, 0xde, 0xbe, 0xba, 0xfe, 0xca,
};

int main(void)
{
	uint64_t link_addr;

	memcpy(&link_addr, (void*)main_link_addr_sentinel, sizeof(link_addr));

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	printf("main() linked at:  %#llx\n", (unsigned long long)link_addr);
	printf("main() running at: %#lx\n", (unsigned long)(uintptr_t)&main);

	return 0;
}

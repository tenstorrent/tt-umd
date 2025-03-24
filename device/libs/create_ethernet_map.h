/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// External function to create the cluster descriptor yaml file.
extern "C" {
int create_ethernet_map(char *file);
}

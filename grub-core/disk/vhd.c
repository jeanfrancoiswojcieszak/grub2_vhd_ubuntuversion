/* vhd.c - command to add vhd devices.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_vhd
{
  char *devname;
  grub_file_t file;
  struct grub_vhd *next;
  unsigned long id;
};

struct vhddynheader
{
	char cookie[8];
	grub_uint64_t dataoffset;
	grub_uint64_t BAT;
	grub_uint32_t headerversion;
	grub_uint32_t maxentries;
	grub_uint32_t blocsize;
	grub_uint32_t checksum;
	char parentUID[16];
	grub_uint32_t timestamp;
	grub_uint32_t reserved1;
	char parentuincodename[512];
	char parentlocatorentry1[24];
	char parentlocatorentry2[24];
	char parentlocatorentry3[24];
	char parentlocatorentry4[24];
	char parentlocatorentry5[24];
	char parentlocatorentry6[24];
	char parentlocatorentry7[24];
	char parentlocatorentry8[24];
	char reserved2[256];
};


static struct grub_vhd *vhd_list;
static unsigned long last_id = 0;

static const struct grub_arg_option options[] =
  {
    /* TRANSLATORS: The disk is simply removed from the list of available ones,
       not wiped, avoid to scare user.  */
    {"delete", 'd', 0, N_("Delete the specified vhd drive."), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };



/* Delete the vhd device NAME.  */
static grub_err_t
delete_vhd (const char *name)
{
  struct grub_vhd *dev;
  struct grub_vhd **prev;

  /* Search for the device.  */
  for (dev = vhd_list, prev = &vhd_list;
       dev;
       prev = &dev->next, dev = dev->next)
    if (grub_strcmp (dev->devname, name) == 0)
      break;

  if (! dev)
    return grub_error (GRUB_ERR_BAD_DEVICE, "device not found");

  /* Remove the device from the list.  */
  *prev = dev->next;

  grub_free (dev->devname);
  grub_file_close (dev->file);
  grub_free (dev);

  return 0;
}

/* The command to add and remove vhd devices.  */
static grub_err_t
grub_cmd_vhd (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file;
  struct grub_vhd *newdev;
  grub_err_t ret;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "device name required");

  /* Check if `-d' was used.  */
  if (state[0].set)
      return delete_vhd (args[0]);

  if (argc < 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  file = grub_file_open (args[1] , GRUB_FILE_TYPE_VHD
                         | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (! file)
    return grub_errno;

  /* First try to replace the old device.  */
  for (newdev = vhd_list; newdev; newdev = newdev->next)
    if (grub_strcmp (newdev->devname, args[0]) == 0)
      break;

  if (newdev)
    {
      grub_file_close (newdev->file);
      newdev->file = file;

      return 0;
    }

  /* Unable to replace it, make a new entry.  */
  newdev = grub_malloc (sizeof (struct grub_vhd));
  if (! newdev)
    goto fail;

  newdev->devname = grub_strdup (args[0]);
  if (! newdev->devname)
    {
      grub_free (newdev);
      goto fail;
    }

  newdev->file = file;
  newdev->id = last_id++;

  /* Add the new entry to the list.  */
  newdev->next = vhd_list;
  vhd_list = newdev;

  return 0;

fail:
  ret = grub_errno;
  grub_file_close (file);
  return ret;
}

static int
grub_vhd_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		       grub_disk_pull_t pull)
{
  struct grub_vhd *d;
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;
  for (d = vhd_list; d; d = d->next)
    {
      if (hook (d->devname, hook_data))
	return 1;
    }
  return 0;
}

static grub_err_t
grub_vhd_open (const char *name, grub_disk_t disk)
{
  struct grub_vhd *dev;

  for (dev = vhd_list; dev; dev = dev->next)
    if (grub_strcmp (dev->devname, name) == 0)
      break;

  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");

 
  disk->max_agglomerate = 1 << (29 - GRUB_DISK_SECTOR_BITS
				- GRUB_DISK_CACHE_BITS);

  disk->total_sectors = GRUB_DISK_SIZE_UNKNOWN;

  disk->id = dev->id;
  disk->data = dev;


  return 0;
}

static grub_err_t
grub_vhd_read (grub_disk_t disk, grub_disk_addr_t sector,
		    grub_size_t size, char *buf)
{
  grub_file_t file = ((struct grub_vhd *) disk->data)->file;

  struct vhddynheader dynheader;
  grub_file_seek(file,512);
  grub_file_read(file,&dynheader,1024);

  grub_uint32_t density=(grub_swap_bytes32(dynheader.blocsize))>>9;
  grub_uint64_t offsetbloc;
  long long unsigned int nb_bloc=grub_divmod64(sector,density,&offsetbloc);

  grub_file_seek (file,grub_swap_bytes64(dynheader.BAT)+(nb_bloc<<2));
  grub_uint32_t blocpos;
  grub_file_read(file,&blocpos,sizeof(blocpos));

  long long unsigned int finalpos=(long long unsigned int)((grub_swap_bytes32(blocpos)+1+offsetbloc)<<9);

  grub_file_seek (file,finalpos);

  grub_file_read (file, buf, (grub_uint64_t)size << GRUB_DISK_SECTOR_BITS);


  if (grub_errno)
    return grub_errno;

  return 0;
}

static grub_err_t
grub_vhd_write (grub_disk_t disk __attribute ((unused)),
		     grub_disk_addr_t sector __attribute ((unused)),
		     grub_size_t size __attribute ((unused)),
		     const char *buf __attribute ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		     "vhd write is not supported");
}

static struct grub_disk_dev grub_vhd_dev =
  {
    .name = "vhd",
    .id = GRUB_DISK_DEVICE_VHD_ID,
    .disk_iterate = grub_vhd_iterate,
    .disk_open = grub_vhd_open,
    .disk_read = grub_vhd_read,
    .disk_write = grub_vhd_write,
    .next = 0
  };

static grub_extcmd_t cmd;

GRUB_MOD_INIT(vhd)
{
  cmd = grub_register_extcmd ("vhd", grub_cmd_vhd, 0,
			      N_("[-d] DEVICENAME FILE."),
			      /* TRANSLATORS: The file itself is not destroyed
				 or transformed into drive.  */
			      N_("Make a virtual drive from a file."), options);
  grub_disk_dev_register (&grub_vhd_dev);
}

GRUB_MOD_FINI(vhd)
{
  grub_unregister_extcmd (cmd);
  grub_disk_dev_unregister (&grub_vhd_dev);
}

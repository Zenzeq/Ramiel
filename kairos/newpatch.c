/*
 * newpatch.c - functions patching unpacked iOS IM4P bootloader files
 *
 * Copyright 2020 dayt0n
 *
 * This file is part of kairos.
 *
 * kairos is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * kairos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kairos.  If not, see <https://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include "newpatch.h"

#ifdef _WIN32
void *memmem(const void *haystack, size_t haystack_len, 
    const void * const needle, const size_t needle_len)
{
    if (haystack == NULL) return NULL; // or assert(haystack != NULL);
    if (haystack_len == 0) return NULL;
    if (needle == NULL) return NULL; // or assert(needle != NULL);
    if (needle_len == 0) return NULL;

    for (const char *h = haystack;
            haystack_len >= needle_len;
            ++h, --haystack_len) {
        if (!memcmp(h, needle, needle_len)) {
            return h;
        }
    }
    return NULL;
}
#endif

/* begin functions from iBoot32Patcher by iH8sn0w*/
bool has_magic(uint8_t* buf) {
	uint32_t magic;
	magic = *(uint32_t*)(buf+7);
	if(memcmp(&magic,IMAGE4_MAGIC,4) == 0) {
		return true;
	}
	return false;
}

bool has_kernel_load_k(struct iboot64_img* iboot_in) {
	void* debug_enabled_str = memmem(iboot_in->buf, iboot_in->len, KERNELCACHE_PREP_STRING,strlen(KERNELCACHE_PREP_STRING));
	return (bool) (debug_enabled_str != NULL);
}

bool has_recovery_console_k(struct iboot64_img* iboot_in) {
	void* entering_recovery_str = memmem(iboot_in->buf, iboot_in->len, ENTERING_RECOVERY_CONSOLE,strlen(ENTERING_RECOVERY_CONSOLE));
	return (bool) (entering_recovery_str != NULL);
}

void* iboot64_memmem(struct iboot64_img* iboot_in, void* pat) { // slightly modified from ih8sn0w's iboot_memmem()
	uint64_t new_pat = (uint64_t)GET_IBOOT64_ADDR(iboot_in, pat);
	return (void*) memmem(iboot_in->buf,iboot_in->len,&new_pat,sizeof(uint64_t));
}

uint64_t get_iboot64_base_address(struct iboot64_img* iboot_in) {  // modified from ih8sn0w's get_iboot_base_address()
	uint32_t offset = 0x318;
	get_iboot64_version(iboot_in);
	if(iboot_in->buf) {
		if(iboot_in->VERS >= 6603) // as of iOS 14, the base address appears to have been moved to 0x300
			offset = 0x300;
		iboot_in->base = *(uint64_t*)(iboot_in->buf + offset);
		return iboot_in->base;
	}
	return 0;
}
/* end functions from iBoot32Patcher */

uint32_t get_iboot64_version(struct iboot64_img* iboot_in) {
	void* versionString = memmem(iboot_in->buf,iboot_in->len,"iBoot-",strlen("iBoot-"));
	if(!versionString) {
		return 0;
	}
	char vers[5];
	// get major version
	strncpy(vers,versionString+6,4);
	iboot_in->VERS = atoi(vers);
	// get minor version
	bzero(vers,5);
	strncpy(vers,versionString+11,4);
	char* period_loc = strchr(vers,'.');
	*period_loc = '\0';
	iboot_in->minor_vers = atoi(vers);
	return iboot_in->VERS; // return major version
}

uint64_t iboot64_ref(struct iboot64_img* iboot_in, void* pat) {
	uint64_t new_pat = (uintptr_t) GET_IBOOT64_ADDR(iboot_in, pat);
	addr_t ref = xref64(iboot_in->buf,0,iboot_in->len,new_pat-iboot_in->base);
	if(!ref) {
		return -1;
	}
	return ref;
}

// inspiration for these functions from tihmstar/ih8sn0w
int change_bootarg_adr_xref_addr(struct iboot64_img* iboot_in, addr_t dest, addr_t address) {
	// get instruction type
	uint32_t insn = get_insn(iboot_in->buf,dest);
	insn_type_t type = get_type(insn);
	if(type == unknown) {
		return -1;
	}
	if(type == adr || type == nop) {
		uint32_t newAdr = 0;
        if ((iboot_in->VERS >= 6723 && iboot_in->minor_vers >= 100) || iboot_in->VERS >= 7429)
			newAdr = new_insn_adr(dest,24,address-(addr_t)iboot_in->buf);
		else
			newAdr = replace_adr_addr(dest,insn,address-(addr_t)iboot_in->buf);
		if (newAdr == -1) {
			WARN("Address too far away\n");
			return -1;
		}
		write_opcode(iboot_in->buf,dest,newAdr);
	}
	return 0;
}

int doFinalBootArgs(struct iboot64_img* iboot_in, addr_t xref, addr_t default_args_loc) {
    if ((iboot_in->VERS >= 6723 && iboot_in->minor_vers >= 100) || iboot_in->VERS >= 7429) // not necessary as of iOS 14.5
		return 0;
	uint32_t adrInsn = get_insn(iboot_in->buf,xref);
	uint8_t rd = get_rd(adrInsn);
	// find next csel
	addr_t temp = xref;
	while(get_type(get_insn(iboot_in->buf,temp)) != csel)
		temp += 4;
	uint32_t cselInsn = get_insn(iboot_in->buf,temp);
	if(get_rn(cselInsn) != rd && get_rm(cselInsn) != rd) {
		WARN("CSEL instruction does not compare the same register as the previous ADR instruction\n");
		return -1;
	}
	uint32_t movInsn = new_mov_register_insn(get_rd(cselInsn),-1,rd,0); // change csel to mov, no conditions here. mov x# boot-arg-addr
	write_opcode(iboot_in->buf,temp,movInsn);
	LOG("Changed CSEL to MOV\n");
	// now we need to look for the bl instruction before this entire method
	temp -=4; // keep our distance
	while(get_supertype(get_insn(iboot_in->buf,temp)) != supertype_branch_immediate || get_type(get_insn(iboot_in->buf,temp)) == bl)
		temp -=4;
	int64_t bImmediate = 0;
	if(get_type(get_insn(iboot_in->buf,temp)) == cbz || get_type(get_insn(iboot_in->buf,temp)) == bcond )
		bImmediate = get_addr_for_cbz(temp,get_insn(iboot_in->buf,temp));
	else {
		WARN("Something went wrong when finding branch instructions\n");
		return -1;
	}
	LOG("Found branch pointing to 0x%llx at 0x%llx\n",((bImmediate-(temp/4))+temp)+iboot_in->base,temp);
	temp = ((bImmediate-(temp/4))+temp); // set temp to the addr that bl goes to
	for(int i = 0; i < 15; i++) { // look for next adr instruction. honestly, this may not even be needed
	// it doesn't seem to do any damage, but for older iboots boot args don't seem to have a conditional branch where a dst reg is replaced
	// and the csel -> mov insn really seems to do what we want here anyway...
		if (temp > iboot_in->len)
			break;
		if(get_type(get_insn(iboot_in->buf,temp)) != adr)
			temp += 4;
		else {
			int64_t oldAddr = get_addr_for_adr(temp,get_insn(iboot_in->buf,temp));
			uint8_t oldRD = get_rd(get_insn(iboot_in->buf,temp));
			uint32_t newAdr = replace_adr_addr(temp,get_insn(iboot_in->buf,temp),default_args_loc-(addr_t)iboot_in->buf); // replace with boot-arg location
			write_opcode(iboot_in->buf,temp,newAdr);
			LOG("Changed ADR X%d, 0x%llx to ADR X%d, 0x%llx\n",oldRD,((oldAddr-(temp/4))+temp)+iboot_in->base,get_rd(newAdr),(default_args_loc-(addr_t)iboot_in->buf)+iboot_in->base);
			break;
		}
	}
	return 0;
}

void do_kdbg_mov(struct iboot64_img* iboot_in, addr_t xref) {
	// find second bl instruction
	xref = get_next_nth_insn(iboot_in->buf,xref,2,bl);
	LOG("Found second bl after debug-enabled xref at 0x%llx\n",xref);
	// now we need to change this bl to movz x0, #1
	uint32_t movOp = new_mov_immediate_insn(0,1,1);
	write_opcode(iboot_in->buf,xref,movOp);
	LOG("Wrote MOVZ X0, #1 to 0x%llx\n",xref+iboot_in->base);
}

bool checkIMG4Ref(uint8_t* buf, addr_t xref) {
	xref -= 4;
	for(int i = 0; i < 10; i++) {
		/*
		 * looking for
		 * add x2, sp, #0x...
		 * add x3, sp, #0x...
		*/
		if((get_type(get_insn(buf,xref)) == add) && (get_rd(get_insn(buf,xref)) == 3) && (get_rn(get_insn(buf,xref)) == 0x1f)) {
			return true;
		}
		xref -= 4;
	}
	return false;
}

void do_rsa_sigcheck_patch(struct iboot64_img* iboot_in, addr_t img4Xref ) {
	addr_t img4refFtop = bof64(iboot_in->buf, 0, img4Xref);
	LOG("Found beginning of _image4_get_partial at 0x%llx\n",img4refFtop);
	// older iBoot versions don't work with this patch method
	// iPatcher, made by @exploit3dguy has some really effective patches for old iBoots
	if (iboot_in->VERS < 3393) { // less than iOS 10
		void* movkLoc = NULL;
		uint32_t movkInsn = 0;
		if (iboot_in->VERS < 2261) // iOS 7
			movkInsn = new_movk_insn(11,0x4348,0,0); // MOVK W11, #0x4348
		else if (iboot_in->VERS < 2817) // iOS 8
			movkInsn = new_movk_insn(10,0x4348,0,0); // MOVK W10, #0x4348
		else // otherwise, it is iOS 9
			movkInsn = new_movk_insn(8,0x4348,0,0); // MOVK W8, #0x4348
		movkLoc = memmem(iboot_in->buf,iboot_in->len,&movkInsn,sizeof(movkInsn));
		if (!movkLoc) {
			WARN("Could not find MOVK W%llu, #0x4348 instruction for old iBoot\n", BIT_RANGE(movkInsn,0,4));
			WARN("RSA PATCH FAILED\n");
			return;
		}
		addr_t movkOffset = (addr_t)GET_IBOOT_FILE_OFFSET(iboot_in,movkLoc);
		LOG("Found MOVK W%llu, #0x4348 at 0x%llx\n",BIT_RANGE(movkInsn,0,4), movkOffset);
		addr_t funcStart = bof64(iboot_in->buf,0,movkOffset); // find beginning of img4 check function
		LOG("Patching RSA check at 0x%llx\n",funcStart + iboot_in->base);
		uint32_t finalMovInsn = new_mov_immediate_insn(0,0,1);
		uint32_t retInsn = new_ret_insn(-1);
		// make it return 0
		write_opcode(iboot_in->buf,funcStart,finalMovInsn); // mov x0, #0
		write_opcode(iboot_in->buf,funcStart+4,retInsn);    // ret
		LOG("Did MOV r0, #0 and RET\n");
		return;
	}
	// jump around
	addr_t img4GetPartialRef = xref64code(iboot_in->buf, 0, iboot_in->len, img4refFtop);
	for(int i = 0; i < 20; i++) {
		if(checkIMG4Ref(iboot_in->buf,img4GetPartialRef))
			break;
		img4GetPartialRef = xref64code(iboot_in->buf,img4GetPartialRef+4,iboot_in->len-img4GetPartialRef-4,img4refFtop);
		if(i == 19) {
			WARN("Could not find correct xref for _image4_get_partial.\n");
			WARN("RSA PATCH FAILED\n");
			return;
		}
	}
	LOG("Found xref to _image4_get_partial at 0x%llx\n",img4GetPartialRef);
	addr_t getPartialRefFtop = bof64(iboot_in->buf,0,img4GetPartialRef);
	LOG("Found start of sub_%llx\n",iboot_in->base+getPartialRefFtop);
	addr_t x2_adr = 0;
	addr_t x3_adr = 0;
	while(1) {
		getPartialRefFtop += 4;
		if(get_type(get_insn(iboot_in->buf,getPartialRefFtop)) == adr && get_rd(get_insn(iboot_in->buf,getPartialRefFtop)) == 2)
			x2_adr = getPartialRefFtop;
		else if(get_type(get_insn(iboot_in->buf,getPartialRefFtop)) == adr && get_rd(get_insn(iboot_in->buf,getPartialRefFtop)) == 3)
			x3_adr = getPartialRefFtop;
		else if(get_type(get_insn(iboot_in->buf,getPartialRefFtop)) == bl) {
			if(x2_adr && x3_adr)
				break;
			else {
				x2_adr = 0;
				x3_adr = 0;
			}
		}
	}
	int64_t verifyRef = get_addr_for_adr(x2_adr,get_insn(iboot_in->buf,x2_adr));
	LOG("Found ADR X2, 0x%llx at 0x%llx\n",verifyRef-(x2_adr/4)+x2_adr+iboot_in->base,x2_adr);
	addr_t verifyFunc = (get_ptr_loc(iboot_in->buf,verifyRef-(x2_adr/4)+x2_adr)-iboot_in->base); // dereference
	if (verifyFunc > (addr_t)(iboot_in->buf+iboot_in->len)) { // in older versions a dereference does not need to be made
		verifyFunc = verifyRef-(x2_adr/4)+x2_adr;
	}
    if (iboot_in->VERS < 5540) {
        LOG("Call to 0x%llx\n",verifyFunc);
        addr_t crawl = verifyFunc;
        crawl += 4;
        while(get_type(get_insn(iboot_in->buf,crawl)) != ret) {
            crawl += 4;
        }
        LOG("RET found for sub_%llx at 0x%llx\n",verifyFunc+iboot_in->base,crawl);
        uint32_t movInsn = new_mov_immediate_insn(0,0,1);
        uint32_t retInsn = new_ret_insn(-1);
        write_opcode(iboot_in->buf,crawl,movInsn);
        write_opcode(iboot_in->buf,crawl+4,retInsn);
    } else {
        LOG("Call to sub_%llx\n",verifyFunc);
        // just patch at beginning, doesn't seem like this actually harms anything
        uint32_t movInsn = new_mov_immediate_insn(0,0,1);
        uint32_t retInsn = new_ret_insn(-1);
        write_opcode(iboot_in->buf,verifyFunc,movInsn);
        write_opcode(iboot_in->buf,verifyFunc+4,retInsn);
    }
	LOG("Did MOV r0, #0 and RET\n");
}

int patch_boot_args64(struct iboot64_img* iboot_in, char* bootargs) {
	// find current boot-args
	void* default_loc = NULL;
	int num = 1;
	LOG("Image base address at 0x%llx\n",iboot_in->base);
	if ((iboot_in->VERS >= 6723 && iboot_in->minor_vers >= 100) || iboot_in->VERS >= 7429)
		default_loc = memmem(iboot_in->buf,iboot_in->len,"rd=md0",strlen("rd=md0"));
	else
		default_loc = memmem(iboot_in->buf,iboot_in->len,DEFAULT_BOOTARGS_STRING,strlen(DEFAULT_BOOTARGS_STRING));
	if(!default_loc) { // if those are not found, try for the other possible string
		LOG("Searching for alternate boot-args\n");
        if ((iboot_in->VERS >= 6723 && iboot_in->minor_vers >= 100) || iboot_in->VERS >= 7429)
			default_loc = memmem(iboot_in->buf,iboot_in->len," -progress",strlen(" -progress"));
		else
			default_loc = memmem(iboot_in->buf,iboot_in->len,OTHER_DEFAULT_BOOTARGS_STRING,strlen(OTHER_DEFAULT_BOOTARGS_STRING));
		if(!default_loc) { // failed, uh oh
			WARN("Could not find boot-arg string\n");
			return -1;
		}
	}
	LOG("Found boot-arg string at %p\n",GET_IBOOT_FILE_OFFSET(iboot_in,default_loc));
	uint64_t default_args_xref = iboot64_ref(iboot_in,default_loc);
    if ((iboot_in->VERS >= 6723 && iboot_in->minor_vers >= 100) || iboot_in->VERS >= 7429) {
		LOG("Relocating from 0x%llx...\n",iboot_in->base+default_args_xref);
		default_args_xref = get_next_nth_insn(iboot_in->buf,default_args_xref,5,nop);
	}
	if(!default_args_xref) {
		WARN("Could not find boot-arg xref\n");
		return -1;
	}
	LOG("Found boot-arg xref at 0x%llx\n",iboot_in->base+default_args_xref);
	// we only do xref relocation now. its cooler that way
	if(strlen(bootargs) > 270) {
		char bootargCpy[271] = { '\0' }; // sorry, gotta shorten it
		strncpy(bootargCpy,bootargs,270);
		bootargs = bootargCpy;
		num = 0;
		WARN("Truncated boot-args: %s\n",bootargs);
	}
	if(strlen(bootargs) < 87) {  // TODO: fix size stuff here 
		void* cert_loc = NULL;
		char zeros[270] = {0};
		// cryptiiiic boot arg location
		// https://github.com/Cryptiiiic/liboffsetfinder64/blob/4d034e5102178177e1bf9f5cc024a95651bed22b/liboffsetfinder64/ibootpatchfinder64_base.cpp#L225
		// so much better than finding random error strings
		cert_loc = memmem(iboot_in->buf,iboot_in->len,zeros,270);
		if(!cert_loc) {
			cert_loc = memmem(iboot_in->buf,iboot_in->len,CERT_STRING,strlen(CERT_STRING));
			if(!cert_loc) {
				WARN("Could not find long string to override\n");
				return -1; // no Reliance string or dart_ctrr. update code
			}
		} else {
			cert_loc += 0x10;
		}
		LOG("Pointing boot-arg xref to large string at: %p\n",GET_IBOOT64_ADDR(iboot_in,cert_loc));
		bzero(cert_loc,270); // zero out cert_loc
		int ret = change_bootarg_adr_xref_addr(iboot_in,default_args_xref,(unsigned long long)cert_loc);
		if (ret < 0)
			return -1;
		default_loc = cert_loc;
	}
	else if(strlen(bootargs) > 270) {
		WARN("Boot-arg string is too long!\n");
		return -1;
	} else {
		memset(default_loc,' ',strlen(DEFAULT_BOOTARGS_STRING)); // zero out OG boot-arg string
	}
	strncpy(default_loc,bootargs,strlen(bootargs)+num); // main part done. also no null terminator. don't like those
	// now to patch up
	return doFinalBootArgs(iboot_in,default_args_xref,(unsigned long long)default_loc);
}

int enable_kernel_debug(struct iboot64_img* iboot_in) {
	void* debugLoc = NULL;
	debugLoc = memmem(iboot_in->buf,iboot_in->len,"debug-enabled",strlen("debug-enabled"));
	if(!debugLoc) {
		WARN("Could not find debug-enabled string\n");
		return -1;
	}
	LOG("Found debug-enabled string at %p\n",GET_IBOOT_FILE_OFFSET(iboot_in,debugLoc));
	uint64_t debugEnabledXref = iboot64_ref(iboot_in,debugLoc);
	if(!debugEnabledXref) {
		WARN("Could not find debug-enabled xref\n");
		return -1;
	}
	LOG("Found debug-enabled xref at 0x%llx\n",debugEnabledXref);
	// now hand off ctrl to patchfinder to get rid of bl and replace with an unconditional mov
	do_kdbg_mov(iboot_in,debugEnabledXref);
	LOG("Enabled kernel debug\n");
	return 0;
}

int rsa_sigcheck_patch(struct iboot64_img* iboot_in) {
	void* img4Loc = NULL;
	img4Loc = memmem(iboot_in->buf,iboot_in->len,"IMG4",4);
	if(!img4Loc) {
		WARN("Could not find IMG4 string\n");
		return -1;
	}
	LOG("Found IMG4 string at %p\n",GET_IBOOT_FILE_OFFSET(iboot_in,img4Loc));
	uint64_t img4Ref = iboot64_ref(iboot_in,img4Loc);
	if(!img4Ref) {
		WARN("Could not find IMG4 xref\n");
		return -1;
	}
	LOG("Found IMG4 xref at 0x%llx\n",img4Ref);
	do_rsa_sigcheck_patch(iboot_in, img4Ref);
	return 0;
}

int do_command_handler_patch(struct iboot64_img* iboot_in, char* command, uintptr_t ptr) { // useful for kicking off iBoot payloads
	char* realCmd = (char*)malloc(strlen(command)+2); // nulls all around
	memset(realCmd,0,strlen(command)+2); 
	// need to search for null-surrounded \0__cmd__\0
	for(int i = 0; i < (strlen(command)); i++)
		realCmd[i+1] = command[i];
	void* cmdLoc = memmem(iboot_in->buf,iboot_in->len,realCmd,strlen(command)+2);
	if(!cmdLoc) {
		WARN("Unable to find \"%s\" in image\n",command);
		free(realCmd);
		return -1;
	}
	free(realCmd); // into the garbage
	cmdLoc++; // because we had a null char at the beginning, advance one to get real location
	LOG("Found command \"%s\" at %p looking for 0x%llx\n",command,GET_IBOOT_FILE_OFFSET(iboot_in,cmdLoc),(uint64_t)GET_IBOOT64_ADDR(iboot_in,cmdLoc));
	void* cmdRef = iboot64_memmem(iboot_in,cmdLoc);
	if(!cmdRef) {
		WARN("Unable to find reference to command \"%s\"\n",command);
		return -1;
	}
	addr_t ref = (addr_t)GET_IBOOT_FILE_OFFSET(iboot_in,cmdRef);
	LOG("Found reference to %s command at 0x%llx\n",command,ref);
	// now do patch
	LOG("Pointing %s to 0x%lx\n",command,ptr);
	*(uint64_t*)(iboot_in->buf+ref+8) = ptr; // plus 8 because we don't want to overwrite the ref itself, but what it executes
	return 0;
}

int unlock_nvram(struct iboot64_img* iboot_in) {
	void* debuguartLoc = memmem(iboot_in->buf,iboot_in->len,"debug-uarts",strlen("debug-uarts"));
	if(!debuguartLoc) {
		WARN("Unable to find debug-uarts string\n");
		return -1;
	}
	LOG("Found debug-uarts string at %p\n",GET_IBOOT64_ADDR(iboot_in,debuguartLoc));
	void* debuguartRef = iboot64_memmem(iboot_in,debuguartLoc);
	if(!debuguartRef) {
		WARN("Unable to find debug-uarts reference\n");
		return -1;
	}
	addr_t debugRef = (addr_t)GET_IBOOT_FILE_OFFSET(iboot_in,debuguartRef);
	LOG("Found debug-uarts reference at 0x%llx\n",debugRef);
	addr_t setenvWhitelist = debugRef;
	while(get_ptr_loc(iboot_in->buf,setenvWhitelist-=8)); // move back until we get 0x0
	setenvWhitelist+=8; // go back up one to get to start of list
	LOG("setenv whitelist begins at 0x%llx\n",setenvWhitelist);
	addr_t blacklistFunc = xref64(iboot_in->buf,0,iboot_in->len,setenvWhitelist);
	if(!blacklistFunc) {
		WARN("Could not find reference to setenv whitelist\n");
		return -1;
	}
	LOG("Found ref to setenv whitelist at 0x%llx\n",blacklistFunc);
	addr_t blacklistFuncBegin = bof64(iboot_in->buf,0,blacklistFunc);
	if(!blacklistFuncBegin) {
		WARN("Could not find beginning of blacklist function\n");
		return -1;
	}
	LOG("Forcing sub_%llx to return immediately\n",blacklistFuncBegin+iboot_in->base);
	uint32_t movZeroZero = new_mov_immediate_insn(0,0,1);
	uint32_t retInsn = new_ret_insn(-1);
	write_opcode(iboot_in->buf,blacklistFuncBegin,movZeroZero);
	write_opcode(iboot_in->buf,blacklistFuncBegin+4,retInsn);
	addr_t envWhitelist = setenvWhitelist;
	while(get_ptr_loc(iboot_in->buf,envWhitelist+=8));
	envWhitelist += 8;
	LOG("Found env whitelist at 0x%llx\n",envWhitelist);
	addr_t blacklistFunc2 = xref64(iboot_in->buf,0,iboot_in->len,envWhitelist);
	if(!blacklistFunc2) {
		WARN("Could not find reference to env whitelist\n");
		return -1;
	}
	LOG("Found ref to env whitelist at 0x%llx\n",blacklistFunc2);
	addr_t blacklistFunc2Begin = bof64(iboot_in->buf,0,blacklistFunc2);
	if(!blacklistFunc2Begin) {
		WARN("Could not find beginning of second blacklist function\n");
		return -1;
	}
	LOG("Forcing sub_%llx to return immediately\n",blacklistFunc2Begin+iboot_in->base);
	write_opcode(iboot_in->buf,blacklistFunc2Begin,movZeroZero);
	write_opcode(iboot_in->buf,blacklistFunc2Begin+4,retInsn);
	void* comAppleSystemLoc = memmem(iboot_in->buf,iboot_in->len,"com.apple.System.",strlen("com.apple.System.")+1);
	// strlen() + 1 because we are including the null terminator in the search 
	if(!comAppleSystemLoc) {
		WARN("Could not find string \"com.apple.System.\"\n");
		return -1;
	}
	LOG("Found \"com.apple.System.\" string at %p\n",GET_IBOOT64_ADDR(iboot_in,comAppleSystemLoc));
	addr_t comAppleSystemRef = iboot64_ref(iboot_in,comAppleSystemLoc);
	if(!comAppleSystemLoc) {
		WARN("Could not find reference to \"com.apple.System.\"\n");
		return -1;
	}
	LOG("Found reference to \"com.apple.System.\" at 0x%llx\n",comAppleSystemRef);
	addr_t appleSystemFuncBegin = bof64(iboot_in->buf,0,comAppleSystemRef);
	if(!appleSystemFuncBegin) {
		WARN("Unable to find beginning of function where \"com.apple.System.\" is referenced\n");
		return -1;
	}
	LOG("Forcing sub_%llx to return immediately\n",appleSystemFuncBegin+iboot_in->base);
	write_opcode(iboot_in->buf,appleSystemFuncBegin,movZeroZero);
	write_opcode(iboot_in->buf,appleSystemFuncBegin+4,retInsn);
	return 0;
}

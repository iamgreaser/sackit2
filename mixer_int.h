void MIXER_NAME(sackit_playback_t *sackit, int offs, int len)
{
	uint32_t tfreq = sackit->freq;

	int i,j;
	int offsend = offs+len;
#ifdef MIXER_STEREO
	int pan, vl, vr;
	offs *= 2;
	offsend *= 2;
#endif

	int16_t *buf = &(sackit->buf[offs]);
	int32_t *mixbuf = (int32_t *)&(sackit->mixbuf[offs]);

	/*
	2.11:
	000010CD  2EA10F04          mov ax,[cs:0x40f] // mixing frequency
	000010D1  BBF401            mov bx,0x1f4 // 500
	000010D4  33D2              xor dx,dx
	000010D6  F7F3              div bx // f/500
	000010D8  40                inc ax // (f/500)+1
	000010D9  2EA35D0C          mov [cs:0xc5d],ax // ramp length
	000010DD  C1E003            shl ax,byte 0x3
	000010E0  2EA35F0C          mov [cs:0xc5f],ax

	2.12:
	0000199E  2EA10F0C          mov ax,[cs:0xc0f]
	000019A2  BB9001            mov bx,0x190 // 400
	000019A5  33D2              xor dx,dx
	000019A7  F7F3              div bx // f/400
	000019A9  40                inc ax // (f/400)+1
	000019AA  2EA36014          mov [cs:0x1460],ax // ramp length
	000019AE  C1E003            shl ax,byte 0x3
	000019B1  2EA36414          mov [cs:0x1464],ax

	Sometimes I hate being right,
	but it's times like these where I love being right.
	*/
#if MIXER_VER <= 211
	int32_t ramplen = tfreq/500+1;
#else
	int32_t ramplen = tfreq/400+1;
#endif

	int32_t gvol = sackit->gv; // 7
	int32_t mvol = sackit->mv; // 7

	/* 2.12 - notes for anticlick
	0000180A  51                push cx
	0000180B  2E8E06270C        mov es,[cs:0xc27]
	00001810  2E8B0E230C        mov cx,[cs:0xc23]
	00001815  03C9              add cx,cx
	00001817  33FF              xor di,di
	00001819  6633C0            xor eax,eax
	0000181C  F366AB            rep stosd // fill mix(?) buffer with 0s
	0000181F  59                pop cx

	00001820  6660              pushad
	00001822  BB0403            mov bx,0x304 // last-output buffer
	00001825  6633D2            xor edx,edx
	00001828  6633FF            xor edi,edi

	loop{
	0000182B  F7040003          test word [si],0x300
	0000182F  741A              jz 0x184b
	if(new_note || note_cut) {
	00001831  662E2B17          sub edx,[cs:bx]
	00001835  662E2B7F04        sub edi,[cs:bx+0x4]
	0000183A  662EC70700000000   mov dword [cs:bx],0x0
	00001842  662EC7470400000000 mov dword [cs:bx+0x4],0x0
	}
	0000184B  81C68000          add si,0x80
	0000184F  83C308            add bx,byte +0x8
	00001852  49                dec cx
	00001853  75D6              jnz 0x182b
	}

	EDX = leftramp
	EDI = rghtramp

	00001855  668BDA            mov ebx,edx
	00001858  668BC2            mov eax,edx
	0000185B  6699              cdq
	0000185D  662EF73E6014      idiv dword [cs:0x1460] // ramp length
	00001863  668BC8            mov ecx,eax

	00001866  668BC7            mov eax,edi
	00001869  6699              cdq
	0000186B  662EF73E6014      idiv dword [cs:0x1460] // ramp length

	EBX = leftramp
	EDI = rghtramp
	ECX = leftramp/ramplen
	EAX = rghtramp/ramplen

	00001871  33F6              xor si,si
	00001873  2E8B166014        mov dx,[cs:0x1460]
	loop{
	00001878  662BD9            sub ebx,ecx // dec first
	0000187B  662BF8            sub edi,eax
	0000187E  6626891C          mov [es:si],ebx // THEN store
	00001882  6626897C04        mov [es:si+0x4],edi
	00001887  83C608            add si,byte +0x8
	0000188A  4A                dec dx
	0000188B  75EB              jnz 0x1878
	}
	0000188D  6661              popad
	*/

#ifdef MIXER_STEREO
	for(j = 0; j < len*2; j++)
#else
	for(j = 0; j < len; j++)
#endif
		mixbuf[j] = 0;

#ifdef MIXER_ANTICLICK
#ifdef MIXER_STEREO
	if(sackit->anticlick[0] != 0 || sackit->anticlick[1] != 0)
	{
		int32_t rampmul0 = sackit->anticlick[0];
		int32_t rampmul1 = sackit->anticlick[1];
		int32_t rampspd0 = rampmul0;
		int32_t rampspd1 = rampmul1;
		if(rampspd0 < 0) {
			rampspd0 = -rampspd0;
			rampspd0 /= ramplen;
			rampspd0 = -rampspd0;
		} else {
			rampspd0 /= ramplen;
		}
		if(rampspd1 < 0) {
			rampspd1 = -rampspd1;
			rampspd1 /= ramplen;
			rampspd1 = -rampspd1;
		} else {
			rampspd1 /= ramplen;
		}
		sackit->anticlick[0] = 0;
		sackit->anticlick[1] = 0;
#else
	if(sackit->anticlick[0] != 0)
	{
		int32_t rampmul = sackit->anticlick[0];
		int32_t rampspd = rampmul;
		if(rampspd < 0) {
			rampspd = -rampspd;
			rampspd /= ramplen;
			rampspd = -rampspd;
		} else {
			rampspd /= ramplen;
		}
		sackit->anticlick[0] = 0;
#endif

		for(j = 0; j < ramplen; j++)
		{
#ifdef MIXER_STEREO
			rampmul0 -= rampspd0;
			rampmul1 -= rampspd1;
			mixbuf[j*2+0] += (int32_t)rampmul0;
			mixbuf[j*2+1] += (int32_t)rampmul1;
			//mixbuf[j*2] -= ((((int32_t)rampmul0)*(int32_t)(ramplen-j-1))/ramplen)<<16;
			//mixbuf[j*2+1] -= ((((int32_t)rampmul1)*(int32_t)(ramplen-j-1))/ramplen)<<16;
#else
			rampmul -= rampspd;
			mixbuf[j] += (int32_t)rampmul;
			//mixbuf[j] -= ((((int32_t)rampmul)*(int32_t)(ramplen-j-1))/ramplen)<<16;
#endif
		}
	}
#endif

	for(i = 0; i < sackit->achn_count; i++)
	{
		sackit_achannel_t *achn = &(sackit->achn[i]);

		if(achn->sample == NULL || achn->sample->data == NULL
			|| achn->offs >= (int32_t)achn->sample->length
			|| achn->offs < 0)
		{
			achn->flags &= ~(
				SACKIT_ACHN_RAMP
				|SACKIT_ACHN_MIXING
				|SACKIT_ACHN_PLAYING
				|SACKIT_ACHN_SUSTAIN);
		}

		if(achn->flags & SACKIT_ACHN_RAMP)
		{
			achn->flags &= ~SACKIT_ACHN_RAMP;
			//ramprem = rampspd;
			achn->lramp = 0;
			achn->lramp_len = 0;

			//printf("ramp %i %i %i\n", i, rampspd, (32768+rampspd-1)/rampspd);
			//printf("ramp %i %i %i\n", i, rampinc, ramprem);
		}

#ifdef MIXER_ANTICLICK
		achn->anticlick[0] = 0;
		achn->anticlick[1] = 0;
#endif

		// TODO: ramp stereowise properly
		if(achn->flags & SACKIT_ACHN_MIXING)
		{
#ifdef MIXER_STEREO
			pan = achn->pan;
			if(pan == 100)
			{
				vl = 0x100;
				vr = -0x100;
			} else {
				if(pan <= 32)
				{
					vl = 0x100;
					vr = pan<<3;
				} else {
					vl = (64-pan)<<3;
					vr = 0x100;
				}

				int sep = sackit->module->header.sep;
				vl = 0x100 * (128-sep) + vl * sep;
				vr = 0x100 * (128-sep) + vr * sep;
				vl >>= 7;
				vr >>= 7;
			}
#endif

			int32_t zoffs = achn->offs;
			int32_t zsuboffs = achn->suboffs;
			int32_t zfreq = achn->ofreq;

			zfreq = sackit_div_int_32_32_to_fixed_16(zfreq,tfreq);

			//printf("freq %i %i %i\n", zfreq, zoffs, zsuboffs);

			int32_t zlpbeg = achn->sample->loop_begin;
			int32_t zlpend = achn->sample->loop_end;
			int32_t zlength = achn->sample->length;
			uint8_t zflg = achn->sample->flg;
			int16_t *zdata = achn->sample->data;

			if((achn->flags & SACKIT_ACHN_SUSTAIN)
				&& (zflg & IT_SMP_SUSLOOP))
			{
				zlpbeg = achn->sample->susloop_begin;
				zlpend = achn->sample->susloop_end;
				zflg |= IT_SMP_LOOP;
				if(zflg & IT_SMP_SUSBIDI)
				{
					zflg |= IT_SMP_LOOPBIDI;
				} else {
					zflg &= ~IT_SMP_LOOPBIDI;
				}
			}

			if(!(zflg & IT_SMP_LOOPBIDI))
				achn->flags &= ~SACKIT_ACHN_REVERSE;

			// TODO: sanity check somewhere!
			if(zflg & IT_SMP_LOOP)
				zlength = zlpend;
			if(achn->flags & SACKIT_ACHN_REVERSE)
				zfreq = -zfreq;

			int32_t vol = 0x8000;
			// 4) FV = Vol * SV * IV * CV * GV * VEV * NFC / 2^41
			if(sackit->module->header.flags & IT_MOD_INSTR)
			{
				int16_t svol = achn->sv;
				svol *= achn->iv;
				svol >>= 6;

				int32_t bvol = (int32_t)achn->vol;
				bvol *= achn->cv;
				bvol *= achn->fadeout;
				bvol >>= 7;

				// if ins idx >= 128, the calculation breaks here
				// this is because DL=sv, DH=1, and we're doing DX:AX*sv
				bvol *= svol;
				bvol >>= 7;
				bvol *= achn->evol.y;
				bvol >>= 14;
				bvol *= gvol;
				bvol >>= 7;

				vol = bvol;

			} else {
				int32_t bvol = (int32_t)achn->vol;
				bvol *= achn->cv;
				bvol *= (achn->sv<<1);
				bvol >>= 4;
				bvol *= gvol;
				bvol >>= 7;


				vol = bvol;
			}

			// this *appears* to be what happens...
			vol *= mvol;
#ifdef MIXER_STEREO
			vol >>= 8;
#else
			vol >>= 9;
#endif

			if(achn->lramp_len == 0 && vol != achn->lramp) {
				int32_t rampspd = vol - achn->lramp;
				achn->lramp_spd = 0;

				/* 2.11:
				00000DBA  8B440E            mov ax,[si+0xe] // new vol
				00000DBD  2EA3A40D          mov [cs:0xda4],ax // current ramp vol
				00000DC1  2B441E            sub ax,[si+0x1e] // old vol
				00000DC4  99                cwd
				00000DC5  2EF73E5D0C        idiv word [cs:0xc5d] // ramp length
				00000DCA  2EA3860D          mov [cs:0xd86],ax // ramp speed
				*/
				if(rampspd < 0) {
					rampspd = -rampspd;
					rampspd /= ramplen;
					rampspd = -rampspd;
				} else {
					rampspd /= ramplen;
				}

				// Skip ramping if 0
				/* 2.11:
				00000D0D  2EF73E5D0C        idiv word [cs:0xc5d]
				00000D12  2EA3AC0C          mov [cs:0xcac],ax
				00000D16  23C0              and ax,ax
				00000D18  7507              jnz 0xd21
				00000D1A  8B440C            mov ax,[si+0xc]
				00000D1D  2EA3940C          mov [cs:0xc94],ax
				*/
				if(rampspd != 0) {
					achn->lramp_len = ramplen;
					achn->lramp_spd = rampspd;
				}
				//printf("%04X -> %04X : %d\n", achn->lramp, vol, achn->lramp_spd);
			}

			int32_t rampmul = achn->lramp;
			int32_t ramprem = achn->lramp_len;
			int32_t rampspd = achn->lramp_spd;

			for(j = 0; j < len; j++) {
#ifdef MIXER_INTERP_LINEAR
				// get sample value
				int32_t v0 = zdata[zoffs];
				int32_t v1 = ((zoffs+1) == zlength
					? (zflg & IT_SMP_LOOP
						? zdata[zlpbeg]
						: 0)
					: zdata[(zoffs+1)]);
				int32_t v;

				if(zflg & IT_SMP_16BIT) {
					int32_t vdelta = v1-v0;
					vdelta >>= 1;
					vdelta *= zsuboffs;
					vdelta >>= 15;
					v = v0 + vdelta;

				} else {
					v0 >>= 8;
					v1 >>= 8;
					int32_t vdelta = (v1-v0);
					vdelta *= zsuboffs;
					vdelta >>= 8;
					v0 <<= 8;
					v = v0 + vdelta;
				}
#else
				int32_t v = zdata[zoffs];
#endif

				if(ramprem > 0)
				{
					v = v * rampmul;
					rampmul += rampspd;
					ramprem--;
				} else {
					v = v * vol;
				}

				// mix
#ifdef MIXER_STEREO
				mixbuf[j*2] -= v*vl>>8;
				mixbuf[j*2+1] -= v*vr>>8;
#else
				mixbuf[j] -= v;
#endif
#ifdef MIXER_ANTICLICK
#ifdef MIXER_STEREO
				achn->anticlick[0] = -v*vl>>8;
				achn->anticlick[1] = -v*vr>>8;
#else
				achn->anticlick[0] = v;
#endif
#endif

				// update
				zsuboffs += zfreq;
				zoffs += (((int32_t)zsuboffs)>>16);
				zsuboffs &= 0xFFFF;

				if((zfreq < 0
					? zoffs < zlpbeg
					: zoffs >= (int32_t)zlength))
				{
					// TODO: ping-pong/bidirectional loops
					// TODO? speed up for tiny loops?
					if(zflg & IT_SMP_LOOP)
					{
						if(zflg & IT_SMP_LOOPBIDI)
						{
							if(zfreq > 0)
							{
								zoffs = zlpend*2-1-zoffs;
								zfreq = -zfreq;
								zsuboffs = 0x10000-zsuboffs;
								achn->flags |= SACKIT_ACHN_REVERSE;
							} else {
								zoffs = zlpbeg*2-zoffs;
								zfreq = -zfreq;
								zsuboffs = 0x10000-zsuboffs;
								achn->flags &= ~SACKIT_ACHN_REVERSE;
							}
						} else {
							while(zoffs >= (int32_t)zlpend)
								zoffs += (zlpbeg-zlpend);
						}
					} else {
						achn->flags &= ~(
							 SACKIT_ACHN_MIXING
							|SACKIT_ACHN_PLAYING
							|SACKIT_ACHN_SUSTAIN);
						break;
					}
				}
			}

			achn->lramp_len = ramprem;
			achn->lramp = (ramprem == 0 ? vol : rampmul);

			achn->offs = zoffs;
			achn->suboffs = zsuboffs;
		} else if(achn->flags & SACKIT_ACHN_PLAYING) {
			// TODO: update offs/suboffs
		}
	}

	// stick into the buffer
#ifdef MIXER_STEREO
	for(j = 0; j < len*2; j++)
#else
	for(j = 0; j < len; j++)
#endif
	{
		int32_t bv = mixbuf[j];
		bv >>= 13;
		bv += 1;
		bv >>= 1;
		if(bv < -32768) bv = -32768;
		else if(bv > 32767) bv = 32767;

		buf[j] = bv;
	}
}


void MIXER_NAME(sackit_playback_t *sackit, int offs, int len)
{
	uint32_t tfreq = 44100; // TODO define this elsewhere
	
	int i,j;
	int offsend = offs+len;
#ifdef MIXER_STEREO
	int pan;
	float vl, vr;
	offs *= 2;
	offsend *= 2;
#endif
	
	int16_t *buf = &(sackit->buf[offs]);
	float *mixbuf = (float *)&(sackit->mixbuf[offs]);
	
	// just a guess :)
	int32_t ramplen = tfreq/400+1;
	
	float gvol = sackit->gv; // 7
	float mvol = sackit->mv; // 7

#ifdef MIXER_STEREO
	for(j = 0; j < len*2; j++)
#else
	for(j = 0; j < len; j++)
#endif
		mixbuf[j] = 0.0f;
	
#ifdef MIXER_STEREO
	if(sackit->anticlick_f[0] != 0 || sackit->anticlick_f[1] != 0)
	{
		int32_t rampmul0 = sackit->anticlick_f[0];
		int32_t rampmul1 = sackit->anticlick_f[1];
		sackit->anticlick_f[0] = 0.0f;
		sackit->anticlick_f[1] = 0.0f;
#else
	if(sackit->anticlick_f[0] != 0.0f)
	{
		int32_t rampmul = sackit->anticlick_f[0];
		sackit->anticlick_f[0] = 0.0f;
#endif
		
		for(j = 0; j < ramplen; j++)
		{
#ifdef MIXER_STEREO
			mixbuf[j*2] += (((int32_t)rampmul0)*(int32_t)(ramplen-j-1))/ramplen;
			mixbuf[j*2+1] += (((int32_t)rampmul1)*(int32_t)(ramplen-j-1))/ramplen;
#else
			mixbuf[j] += (((int32_t)rampmul)*(int32_t)(ramplen-j-1))/ramplen;
#endif
		}
	}

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
			achn->lramp_f = 0;
			
			//printf("ramp %i %i %i\n", i, rampspd, (32768+rampspd-1)/rampspd);
			//printf("ramp %i %i %i\n", i, rampinc, ramprem);
		}

		achn->anticlick_f[0] = 0.0f;
		achn->anticlick_f[1] = 0.0f;
		
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

				// TODO: make this more accurate
				int sep = sackit->module->header.sep;
				vl = 0x100 * (128-sep) + vl * sep;
				vr = 0x100 * (128-sep) + vr * sep;
				vl /= 128.0f;
				vr /= 128.0f;
			}
			vl /= 256.0f;
			vr /= 256.0f;
#endif

			int32_t zoffs = achn->offs;
			int32_t zsuboffs = achn->suboffs;
			int32_t zfreq = achn->ofreq;
			float zlramp = achn->lramp_f;
			
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
			
			double vol = mvol*achn->evol.y*achn->vol*achn->sv*achn->iv*achn->cv*gvol*achn->fadeout;
			vol /= 64.0f*64.0f*64.0f*64.0f*128.0f*64.0f*128.0f*1024.0f*512.0f;
			achn->lramp_f = vol;
			
			// TODO: get ramping working correctly
			for(j = 0; j < len; j++) {
#ifdef MIXER_INTERP_LINEAR
				// get sample value
				float v0 = zdata[zoffs];
				float v1 = ((zoffs+1) == zlength
					? (zflg & IT_SMP_LOOP
						? zdata[zlpbeg]
						: 0)
					: zdata[(zoffs+1)]);
				float v  = ((v0*(65535-zsuboffs))
					+ (v1*(zsuboffs)))/32768.0f/65536.0f;
#else
#ifdef MIXER_INTERP_CUBIC
				// get sample value
				// TODO: do this more efficiently / correctly
				float v0 = (zoffs-1 < 0 ? 0.0f : zdata[zoffs-1]);
				float v1 = zdata[zoffs];
				float v2 = ((zoffs+1) == zlength
					? (zflg & IT_SMP_LOOP
						? zdata[zlpbeg]
						: 0)
					: zdata[(zoffs+1)]);
				float v3 = ((zoffs+2) == zlength
					? (zflg & IT_SMP_LOOP
						? zdata[zlpbeg+1]
						: 0)
					: zdata[(zoffs+2)]);

				// Reference: http://paulbourke.net/miscellaneous/interpolation/
				float t = zsuboffs/65536.0f;
				float t2 = t*t;
				float t3 = t2*t;

				float a0 = v3 - v2 - v0 + v1;
				float a1 = v0 - v1 - a0;
				float a2 = v2 - v0;
				float a3 = v1;

				float v = t3*a0 + t2*a1 + t*a2 + a3;
				v /= 32768.0f;
#else
				float v = zdata[zoffs]/32768.0f;
#endif
#endif
				if(j < ramplen)
					v *= zlramp + (vol-zlramp)*(j/(float)ramplen);
				else
					v *= vol;

				// mix
#ifdef MIXER_STEREO
				mixbuf[j*2] += v*vl;
				mixbuf[j*2+1] += v*vr;
#else
				mixbuf[j] += v;
#endif
#ifdef MIXER_STEREO
				achn->anticlick_f[0] = v*vl;
				achn->anticlick_f[1] = v*vr;
#else
				achn->anticlick_f[0] = v;
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
		int32_t bv = -mixbuf[j]*32768.0f;
		if(bv < -32768) bv = -32768;
		else if(bv > 32767) bv = 32767;
		
		buf[j] = bv;
	}
}


#include <stdafx.h>
#include "looping.h"

namespace loop_helper {
	console_looping_formatter::console_looping_formatter() {
		*this << "Looping: ";
	}


	console_looping_debug_formatter::console_looping_debug_formatter() {
		*this << "Looping: DBG: ";
	}


	format_samples_ex::format_samples_ex(t_uint64 p_samples, t_uint32 p_sample_rate, unsigned p_extra) {
		m_buffer << pfc::format_time_ex(audio_math::samples_to_time(p_samples, p_sample_rate), p_extra);
		m_buffer << " (" << pfc::format_int(p_samples) << ")";
	}

	const char* format_samples_ex::get_ptr() const {
		return m_buffer;
	}

	format_samples_ex::operator const char*() const {
		return m_buffer;
	}


	void do_crossfade(audio_sample* p_dest, const audio_sample* p_src1, const audio_sample* p_src2, int nch, t_size samples, t_uint ratiostart, t_uint ratioend) {
		audio_sample blend_step =
			static_cast<audio_sample>((ratioend - ratiostart) / 100.0 / samples);
		audio_sample ratio = static_cast<audio_sample>(ratiostart) / 100;
		while (samples) {
			for (int ch = nch - 1; ch >= 0; ch--) {
				*p_dest = *p_src1 + (*p_src2 - *p_src1) * ratio;
				p_dest++; p_src1++; p_src2++;
			}
			samples--;
			ratio += blend_step;
		}
	}

	void do_crossfade(audio_chunk& p_dest, t_size destpos, const audio_chunk& p_src1, t_size src1pos, const audio_chunk& p_src2, t_size src2pos, t_size samples, t_uint ratiostart, t_uint ratioend) {
		if (samples == 0) return; // nothing to do

		// sanity check
		if (p_src1.get_srate() != p_src2.get_srate() ||
		    p_src1.get_channel_config() != p_src2.get_channel_config() ||
		    p_src1.get_channels() != p_src2.get_channels() ||
		    p_dest.get_srate() != p_src1.get_srate() ||
		    p_dest.get_channel_config() != p_src1.get_channel_config() ||
		    p_dest.get_channels() != p_src1.get_channels()) {
			throw exception_unexpected_audio_format_change();
		}
		// length check
		if (p_src1.get_sample_count() < (src1pos + samples) ||
		    p_src2.get_sample_count() < (src2pos + samples)) {
			throw exception_io("p_src1 or p_src2 unsufficient sample");
		}
		p_dest.pad_with_silence(destpos + samples);
		int nch = p_dest.get_channels();
		audio_sample * pd = p_dest.get_data() + (destpos*nch);
		const audio_sample * ps1 = p_src1.get_data() + (src1pos*nch);
		const audio_sample * ps2 = p_src2.get_data() + (src2pos*nch);
		do_crossfade(pd, ps1, ps2, nch, samples, ratiostart, ratioend);
	}

	void do_crossfade(audio_chunk& p_dest, t_size destpos, const audio_chunk& p_src, t_size srcpos, t_size samples, t_uint ratiostart, t_uint ratioend) {
		if (samples == 0) return; // nothing to do

		// sanity check
		if (p_dest.get_srate() != p_src.get_srate() ||
		    p_dest.get_channel_config() != p_src.get_channel_config() ||
		    p_dest.get_channels() != p_src.get_channels()) {
			throw exception_unexpected_audio_format_change();
		}
		// length check
		if (p_dest.get_sample_count() < (destpos + samples) ||
		    p_src.get_sample_count() < (srcpos + samples)) {
			throw exception_io("p_dest or p_src unsufficient sample");
		}
		int nch = p_dest.get_channels();
		audio_sample * pd = p_dest.get_data() + (destpos*nch);
		const audio_sample * ps = p_src.get_data() + (srcpos*nch);
		do_crossfade(pd, pd, ps, nch, samples, ratiostart, ratioend);
	}
}
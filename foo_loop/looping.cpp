
#include <stdafx.h>
#include "looping.h"


namespace loop_helper {
	void open_path_helper(input_decoder::ptr & p_input, file::ptr p_file, const char * path,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) {
		p_abort.check();
		p_input.release();

		TRACK_CODE("input_entry::g_open_for_decoding",
			input_entry::g_open_for_decoding(p_input,p_file,path,p_abort,p_from_redirect)
			);

		
		if (!p_skip_hints) {
			try {
				static_api_ptr_t<metadb_io>()->hint_reader(p_input.get_ptr(),path,p_abort);
			} catch(exception_io_data) {
				//Don't fail to decode when this barfs, might be barfing when reading info from another subsong than the one we're trying to decode etc.
				p_input.release();
				if (p_file.is_valid()) p_file->reopen(p_abort);
				TRACK_CODE("input_entry::g_open_for_decoding",
					input_entry::g_open_for_decoding(p_input,p_file,path,p_abort,p_from_redirect)
					);
			}
		}
	}

	bool parse_entity(const char * & ptr,pfc::string8 & name,pfc::string8 & value) {
		char delimiter = '\0';
		char tmp;
		t_size n = 0;
		while(isspace(*ptr)) ptr++;
		while(tmp = ptr[n], tmp && !isspace(tmp) && tmp != '=') n++;
		if (!ptr[n]) return false;
		name.set_string(ptr, n);
		ptr += n;
		while(isspace(*ptr)) ptr++;
		if (*ptr != '=') return false;
		ptr++;
		// check delimiter
		if (*ptr == '\'' || *ptr == '"') {
			delimiter = *ptr;
			ptr++;
		}
		if (!*ptr) return false;

		n = 0;
		if (delimiter == '\0') {
			while(tmp = ptr[n], tmp && !isspace(tmp) && tmp != ';') n++;
		} else {
			while(tmp = ptr[n], tmp && tmp != delimiter) n++;
		}
		if (!ptr[n]) return false;
		value.set_string(ptr, n);
		ptr += n;
		if (*ptr == delimiter) ptr++;
		while(*ptr == ';' || isspace(*ptr)) ptr++;
		return true;
	}

	t_filestats merge_filestats(const t_filestats & p_src1, const t_filestats & p_src2, int p_merge_type) {
		t_filestats dest;
		dest.m_timestamp = pfc::max_t(p_src1.m_timestamp, p_src2.m_timestamp);
		if (p_merge_type == merge_filestats_sum) {
			dest.m_size = p_src1.m_size + p_src2.m_size;
		} else if (p_merge_type == merge_filestats_max) {
			dest.m_size = pfc::max_t(p_src1.m_size, p_src2.m_size);
		} else {
			throw pfc::exception_not_implemented();
		}
		return dest;
	}

	void loop_type_base::raw_seek(t_uint64 samples, abort_callback& p_abort) {
		set_succ(true);
		get_input()->seek(audio_math::samples_to_time(samples, get_sample_rate()), p_abort);
		set_cur(samples);
	}

	void loop_type_base::raw_seek(double seconds, abort_callback& p_abort) {
		set_succ(true);
		get_input()->seek(seconds, p_abort);
		set_cur(audio_math::time_to_samples(seconds, get_sample_rate()));
	}

	t_size loop_type_base::get_more_chunk(audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort, t_size candidate) {
		t_size samples = 0;
		audio_chunk_impl_temporary ptmp_chunk;
		mem_block_container_impl tmp_block;
		mem_block_container_impl * ptmp_block = nullptr;
		if (p_raw != nullptr) ptmp_block = &tmp_block;
		while (get_succ() && candidate > samples) {
			bool succ;
			if (p_raw != nullptr) {
				succ = get_input_v2()->run_raw(ptmp_chunk,tmp_block,p_abort);
			} else {
				succ = get_input()->run(ptmp_chunk,p_abort);
			}
			set_succ(succ);
			if (succ) {
				t_size newsamples = ptmp_chunk.get_sample_count();
				if (newsamples)
					combine_audio_chunks(p_chunk, p_raw, ptmp_chunk, ptmp_block);
				samples += newsamples;
				add_cur(newsamples);
			}
		}
		return samples;
	}

	loop_event_point_baseimpl::loop_event_point_baseimpl(): 
		flags(on_looping) {}

	loop_event_point_baseimpl::loop_event_point_baseimpl(unsigned p_flags): 
		flags(p_flags) {}

	bool loop_event_point_baseimpl::check_no_looping(loop_type_base::ptr p_input) const {
		if (p_input->get_no_looping()) {
			// no_looping
			return !(flags & on_no_looping);
		} else {
			// looping
			return !(flags & on_looping);
		}
	}

	t_uint64 loop_event_point_baseimpl::get_prepare_position() const {
		return static_cast<t_uint64>(-1);
	}

	void loop_event_point_baseimpl::get_info(file_info&, const char*, t_uint32) {
	}

	bool loop_event_point_baseimpl::has_dynamic_info() const {
		return false;
	}

	bool loop_event_point_baseimpl::set_dynamic_info(file_info&, const char*, t_uint32) {
		return false;
	}

	bool loop_event_point_baseimpl::reset_dynamic_info(file_info&, const char*) {
		return false;
	}

	bool loop_event_point_baseimpl::has_dynamic_track_info() const {
		return false;
	}

	bool loop_event_point_baseimpl::set_dynamic_track_info(file_info&, const char*, t_uint32) {
		return false;
	}

	bool loop_event_point_baseimpl::reset_dynamic_track_info(file_info&, const char*) {
		return false;
	}

	loop_event_point_simple::loop_event_point_simple():
		loop_event_point_baseimpl(on_looping), from(0), to(0), maxrepeats(0), repeats(0) {}

	t_uint64 loop_event_point_simple::get_position() const {
		return from;
	}

	void loop_event_point_simple::get_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) {
		pfc::string8 name;
		t_size prefixlen;
		name << p_prefix;
		prefixlen = name.get_length();

		name.truncate(prefixlen);
		name << "from";
		p_info.info_set(name, format_samples_ex(from, sample_rate));

		name.truncate(prefixlen);
		name << "to";
		p_info.info_set(name, format_samples_ex(to, sample_rate));

		if (maxrepeats) {
			name.truncate(prefixlen);
			name << "maxrepeats";
			p_info.info_set_int(name, maxrepeats);
		}
	}

	bool loop_event_point_simple::has_dynamic_info() const {
		return true;
	}

	bool loop_event_point_simple::set_dynamic_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) {
		pfc::string8 name;
		name << p_prefix << "repeats";
		p_info.info_set_int(name, repeats);
		return true;
	}

	bool loop_event_point_simple::reset_dynamic_info(file_info& p_info, const char* p_prefix) {
		pfc::string8 name;
		name << p_prefix << "repeats";
		return p_info.info_remove(name);
	}

	void loop_event_point_simple::check() const {
		if (from == to) throw exception_loop_bad_point();
	}

	bool loop_event_point_simple::process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) {
		if (check_no_looping(p_input)) return false;
		++repeats;
		if (maxrepeats && repeats>=maxrepeats) return false;
		t_size newsamples = pfc::downcast_guarded<t_size>(from - p_start);
		truncate_chunk(p_chunk,p_raw,newsamples);
		p_input->raw_seek(to, p_abort);
		return true;
	}

	bool loop_event_point_simple::process(loop_type_base::ptr p_input, abort_callback& p_abort) {
		if (check_no_looping(p_input)) return false;
		++repeats;
		if (maxrepeats && repeats>=maxrepeats) return false;
		p_input->raw_seek(to, p_abort);
		return true;
	}

	loop_event_point_end::loop_event_point_end():
		loop_event_point_baseimpl(on_no_looping), position(0) {}

	t_uint64 loop_event_point_end::get_position() const {
		return position;
	}

	void loop_event_point_end::get_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) {
		pfc::string8 name;
		name << p_prefix << "position";
		p_info.info_set(name, format_samples_ex(position, sample_rate));
	}

	void loop_event_point_end::check() const {}

	bool loop_event_point_end::process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback&) {
		if (check_no_looping(p_input)) return false;
		t_size newsamples = pfc::downcast_guarded<t_size>(position - p_start);
		truncate_chunk(p_chunk,p_raw,newsamples);
		p_input->set_eof(); // to eof
		return true;
	}

	bool loop_event_point_end::process(loop_type_base::ptr p_input, abort_callback&) {
		if (check_no_looping(p_input)) return false;
		p_input->set_eof(); // to_eof
		return true;
	}

	loop_type_impl_base::dynamic_update_tracker::dynamic_update_tracker():
		lastupdate(0), updateperiod(0), m_input_switched(false) {}

	double loop_type_impl_base::get_dynamic_updateperiod() const {
		return audio_math::samples_to_time(m_dynamic.updateperiod, m_sample_rate);
	}

	double loop_type_impl_base::get_dynamictrack_updateperiod() const {
		return audio_math::samples_to_time(m_dynamic_track.updateperiod, m_sample_rate);
	}

	void loop_type_impl_base::set_dynamic_updateperiod(double p_time) {
		m_dynamic.updateperiod = audio_math::time_to_samples(p_time, m_sample_rate);
	}

	void loop_type_impl_base::set_dynamictrack_updateperiod(double p_time) {
		m_dynamic_track.updateperiod = audio_math::time_to_samples(p_time, m_sample_rate);
	}

	input_decoder::ptr& loop_type_impl_base::get_input() {
		if (m_current_input.is_empty()) throw pfc::exception_bug_check_v2();
		return m_current_input;
	}

	input_decoder_v2::ptr& loop_type_impl_base::get_input_v2() {
		if (m_current_input_v2.is_empty()) throw pfc::exception_not_implemented();
		return m_current_input_v2;
	}

	void loop_type_impl_base::switch_input(input_decoder::ptr p_input) {
		switch_input(p_input, nullptr);
	}

	void loop_type_impl_base::switch_input(input_decoder::ptr p_input, const char* p_path) {
		// please specify reopen'd input...
		m_current_input = p_input;
		if (m_current_input_v2.is_valid()) m_current_input_v2.release();
		m_current_input->service_query_t(m_current_input_v2);
		m_dynamic.m_input_switched = m_dynamic_track.m_input_switched = true;
		m_current_changed = true;
		if (p_path != nullptr) {
			m_current_path.set_string_(p_path);
			m_current_fileext = pfc::string_filename_ext(p_path);
		} else {
			m_current_path.reset();
			m_current_fileext.reset();
		}
		set_succ(true);
	}
	
	void loop_type_impl_base::switch_points(loop_event_point_list p_list) {
		m_dynamic.m_old_points = m_cur_points;
		m_dynamic_track.m_old_points = m_cur_points;
		m_cur_points = p_list;

		m_perm_by_pos.set_size(m_cur_points.get_count());
		order_helper::g_fill(m_perm_by_pos.get_ptr(), m_perm_by_pos.get_size());
		m_cur_points.sort_get_permutation_t(
			loop_event_compare<loop_event_point::ptr, loop_event_point::ptr>, m_perm_by_pos.get_ptr());
		if (m_cur_points_by_pos != nullptr) delete m_cur_points_by_pos;
		m_cur_points_by_pos = new pfc::list_permutation_t<loop_event_point::ptr>(m_cur_points, m_perm_by_pos.get_ptr(), m_perm_by_pos.get_size());

		m_perm_by_prepos.set_size(m_cur_points.get_count());
		order_helper::g_fill(m_perm_by_prepos.get_ptr(), m_perm_by_prepos.get_size());
		m_cur_points.sort_get_permutation_t(
			loop_event_prepos_compare<loop_event_point::ptr, loop_event_point::ptr>, m_perm_by_prepos.get_ptr());
		if (m_cur_points_by_prepos != nullptr) delete m_cur_points_by_prepos;
		m_cur_points_by_prepos = new pfc::list_permutation_t<loop_event_point::ptr>(m_cur_points, m_perm_by_prepos.get_ptr(), m_perm_by_prepos.get_size());
	}

	pfc::list_permutation_t<loop_event_point::ptr> loop_type_impl_base::get_points_by_pos() {
		if (m_cur_points_by_pos == nullptr) throw pfc::exception_bug_check_v2();
		return *m_cur_points_by_pos;
	}

	pfc::list_permutation_t<loop_event_point::ptr> loop_type_impl_base::get_points_by_prepos() {
		if (m_cur_points_by_prepos == nullptr) throw pfc::exception_bug_check_v2();
		return *m_cur_points_by_prepos;
	}

	t_size loop_type_impl_base::bsearch_points_by_pos(t_uint64 pos, t_size& index) {
		return m_cur_points_by_pos->bsearch_t(loop_event_compare<loop_event_point::ptr, t_uint64>, pos, index);
	}

	t_size loop_type_impl_base::bsearch_points_by_prepos(t_uint64 pos, t_size& index) {
		return m_cur_points_by_prepos->bsearch_t(loop_event_prepos_compare<loop_event_point::ptr, t_uint64>, pos, index);
	}

	t_size loop_type_impl_base::get_nearest_point(t_uint64 pos) {
		t_size nums = get_points_by_pos().get_count();
		if (!nums) return static_cast<t_size>(-1);
		t_size index;
		bsearch_points_by_pos(pos, index);
		if (index < nums) {
			return index;
		} else {
			return static_cast<t_size>(-1);
		}
	}

	void loop_type_impl_base::do_events(t_uint64 p_start, t_uint64 p_end, abort_callback& p_abort) {
		t_size nums = get_points_by_pos().get_count();
		t_size n;
		loop_event_point::ptr point;
		bsearch_points_by_pos(p_start, n);
			
		while (n < nums) {
			point = get_points_by_pos()[n];
			m_nextpointpos = point->get_position();
			if (m_nextpointpos > p_end) {
				m_nextpointpos = pfc::min_t(m_nextpointpos, get_prepare_pos(p_end, nums));
				return;
			}
			if (process_event(point, p_abort)) {
				if (p_end != get_cur() || p_start != get_cur()) {
					// current is updated.
					do_current_events(p_abort);
					return;
				}
			}
			n++;
		}
		m_nextpointpos = static_cast<t_uint64>(-1);
	}

	void loop_type_impl_base::do_events(t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) {
		t_size nums = get_points_by_pos().get_count();
		t_size n;
		loop_event_point::ptr point;
		// skip p_start itself, because it was proceeded as older's end
		bsearch_points_by_pos(p_start+1, n);
		t_uint64 end = p_start + p_chunk.get_sample_count();
		t_size preplen = get_prepare_length(p_start, end, nums);
		if (preplen > 0)
			end += get_more_chunk(p_chunk, p_raw, p_abort, preplen);

		while (n < nums) {
			point = get_points_by_pos()[n];
			m_nextpointpos = point->get_position();
			if (m_nextpointpos > end) {
				m_nextpointpos = pfc::min_t(m_nextpointpos, get_prepare_pos(end, nums));
				return;
			}
			if (process_event(point, p_start, p_chunk, p_raw, p_abort)) {
				// current is updated.
				do_current_events(p_abort);
				return;
			}
			n++;
		}
		m_nextpointpos = static_cast<t_uint64>(-1);
	}

	bool loop_type_impl_base::set_dynamic_info(file_info& p_out) {
		t_uint32 sample_rate = get_sample_rate();
		pfc::string8 name;
		name << get_info_prefix() << "current";
		p_out.info_set(name, format_samples_ex(get_cur(), sample_rate));
		name.reset();
		name << get_info_prefix() << "next_event_pos";
		p_out.info_set(name, m_nextpointpos != static_cast<t_uint64>(-1) ?
			format_samples_ex(m_nextpointpos, sample_rate) : "(nothing or eof)");
		return true;
	}

	bool loop_type_impl_base::reset_dynamic_info(file_info& p_out) {
		bool ret = false;
		pfc::string8 name;
		name << get_info_prefix() << "current";
		ret |= p_out.info_remove(name);
		name.reset();
		name << get_info_prefix() << "next_event_pos";
		ret |= p_out.info_remove(name);
		return ret;
	}

	bool loop_type_impl_base::set_dynamic_info_track(file_info& p_out) {
		if (!m_current_changed) return false;
		bool ret = false;
		pfc::string8 name;
		name << get_info_prefix() << "current_file";
		if (!m_current_fileext.is_empty()) {
			p_out.info_set(name, m_current_fileext);
			ret = true;
		} else {
			ret |= p_out.info_remove(name);
		}
		name.reset();
		if (!m_current_input.is_empty()) {
			name << get_info_prefix() << "current_path_raw";
			p_out.info_set(name, m_current_path);
			ret = true;
		} else {
			ret |= p_out.info_remove(name);
		}
		m_current_changed = false;
		return ret;
	}

	bool loop_type_impl_base::reset_dynamic_info_track(file_info& p_out) {
		if (!m_current_changed) return false;
		bool ret = false;
		pfc::string8 name;
		name << get_info_prefix() << "current_file";
		ret |= p_out.info_remove(name);
		name.reset();
		name << get_info_prefix() << "current_path_raw";
		ret |= p_out.info_remove(name);
		return ret;
	}

	bool loop_type_impl_base::run_common(audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) {
		t_uint64 start = get_cur();
		t_uint retries = 4; // max retries
		while (retries > 0 && get_succ()) {
			get_one_chunk(p_chunk,p_raw,p_abort);
			if (get_succ()) {
				if (m_nextpointpos <= get_cur()) {
					do_events(start,p_chunk,p_raw,p_abort);
				}
			} else {
				if (cfg_loop_debug.get())
					console_looping_debug_formatter() << "dispatch EOF event";
				// try dispatching EOF event;
				do_events(static_cast<t_uint64>(-1), p_abort);
				retries--;
				continue;
			}
			break;
		}
		return get_succ();
	}

	void loop_type_impl_base::get_info_for_points(file_info& p_info, loop_event_point_list& points, const char* p_prefix, t_uint32 p_sample_rate) {
		for (t_size n = 0, m = points.get_count(); n < m; ++n ) {
			loop_event_point::ptr point = points[n];
			pfc::string8 name;
			name << p_prefix << "point_" << pfc::format_int(n, 2) << "_";
			point->get_info(p_info, name, p_sample_rate);
		}
	}

	loop_type_impl_base::loop_type_impl_base():
		m_sample_rate(0), m_cur(0), m_succ(false), m_raw_support(true), m_info_prefix("loop_"), 
		m_cur_points_by_pos(nullptr), m_cur_points_by_prepos(nullptr), m_current_changed(false) {
	}

	loop_type_impl_base::~loop_type_impl_base() {
		if (m_cur_points_by_pos != nullptr) delete m_cur_points_by_pos;
		if (m_cur_points_by_prepos != nullptr) delete m_cur_points_by_prepos;
	}

	t_uint64 loop_type_impl_base::get_cur() const {
		return m_cur;
	}

	t_uint32 loop_type_impl_base::get_sample_rate() const {
		return m_sample_rate;
	}

	bool loop_type_impl_base::get_no_looping() const {
		return m_no_looping || cfg_loop_disable.get();
	}

	void loop_type_impl_base::set_info_prefix(const char* p_prefix) {
		m_info_prefix = p_prefix;
	}

	bool loop_type_impl_base::open_path(file::ptr p_filehint, const char* path, t_input_open_reason p_reason, abort_callback& p_abort, bool p_from_redirect, bool p_skip_hints) {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		bool ret = open_path_internal(p_filehint,path,p_reason,p_abort,p_from_redirect,p_skip_hints);
		if (ret) {
			file_info_impl p_info;
			get_input()->get_info(0, p_info, p_abort);
			m_sample_rate = pfc::downcast_guarded<t_uint32>(p_info.info_get_int("samplerate"));
		}
		return ret;
	}

	void loop_type_impl_base::open_decoding(t_uint32 subsong, t_uint32 flags, abort_callback& p_abort) {
		m_no_looping = (flags & input_flag_simpledecode) != 0;
		if (cfg_loop_debug.get())
			console_looping_debug_formatter() << "open decoding (subsong: " << subsong << " / flags: "  << flags << " / no_looping: " << (m_no_looping?"true":"false") << ")";
		set_dynamic_updateperiod(0.5);
		set_dynamictrack_updateperiod(1.0);
		open_decoding_internal(subsong, flags, p_abort);
		set_cur(0);
		set_succ(true);
		do_current_events(p_abort);
	}

	bool loop_type_impl_base::process_event(loop_event_point::ptr point, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) {
		return point->process(this, p_start, p_chunk, p_raw, p_abort);
	}

	bool loop_type_impl_base::process_event(loop_event_point::ptr point, abort_callback& p_abort) {
		return point->process(this, p_abort);
	}

	bool loop_type_impl_base::run(audio_chunk& p_chunk, abort_callback& p_abort) {
		return run_common(p_chunk,nullptr,p_abort);
	}

	bool loop_type_impl_base::run_raw(audio_chunk& p_chunk, mem_block_container& p_raw, abort_callback& p_abort) {
		if (!get_no_looping() && !is_raw_supported()) throw pfc::exception_not_implemented();
		return run_common(p_chunk,&p_raw,p_abort);
	}

	void loop_type_impl_base::seek(double p_seconds, abort_callback& p_abort) {
		user_seek(p_seconds,p_abort);
	}

	void loop_type_impl_base::seek(t_uint64 p_samples, abort_callback& p_abort) {
		user_seek(p_samples,p_abort);
	}

	bool loop_type_impl_base::get_dynamic_info(file_info& p_out, double& p_timestamp_delta) {
		return get_dynamic_info_t<dispatch_dynamic_info>(p_out,p_timestamp_delta,m_dynamic);
	}

	bool loop_type_impl_base::get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) {
		return get_dynamic_info_t<dispatch_dynamic_track_info>(p_out,p_timestamp_delta,m_dynamic_track);
	}

	void loop_type_impl_base::set_logger(event_logger::ptr ptr) {
		get_input_v2()->set_logger(ptr);
	}

	void loop_type_impl_singleinput_base::open_decoding_internal(t_uint32 subsong, t_uint32 flags, abort_callback& p_abort) {
		get_input()->initialize(subsong, flags, p_abort);
	}

	t_uint32 loop_type_impl_singleinput_base::get_subsong_count() {
		return get_input()->get_subsong_count();
	}

	t_uint32 loop_type_impl_singleinput_base::get_subsong(t_uint32 p_index) {
		return get_input()->get_subsong(p_index);
	}

	void loop_type_impl_singleinput_base::get_info(t_uint32 subsong, file_info& p_info, abort_callback& p_abort) {
		get_input()->get_info(subsong, p_info, p_abort);
	}

	t_filestats loop_type_impl_singleinput_base::get_file_stats(abort_callback& p_abort) {
		return get_input()->get_file_stats(p_abort);
	}

	void loop_type_impl_singleinput_base::close() {
		get_input().release();
	}

	void loop_type_impl_singleinput_base::on_idle(abort_callback& p_abort) {
		get_input()->on_idle(p_abort);
	}

	void input_loop_base::open(file::ptr p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort) {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		open_internal(p_filehint, p_path, p_reason, p_abort);
		PFC_ASSERT(m_looptype.is_valid());
		PFC_ASSERT(m_loopentry.is_valid());
		m_looptype->set_info_prefix(m_info_prefix);
	}

	void input_loop_base::get_info(t_uint32 p_subsong, file_info& p_info, abort_callback& p_abort) {
		m_looptype->get_info(p_subsong,p_info,p_abort);
		pfc::string8 name;
		pfc::string8 buf;
		name << m_info_prefix << "type";
		buf << m_loopentry->get_short_name() << " [" << m_loopentry->get_name() << "]";
		p_info.info_set(name, buf);
		name.reset();
		if (!m_loopcontent.is_empty()) {
			name << m_info_prefix << "content";
			p_info.info_set(name, m_loopcontent);
		}
	}

	void input_loop_base::get_info(file_info& p_info, abort_callback& p_abort) {
		get_info(0,p_info,p_abort);
	}

	t_uint32 input_loop_base::get_subsong_count() {
		return m_looptype->get_subsong_count();
	}

	t_uint32 input_loop_base::get_subsong(t_uint32 p_index) {
		return m_looptype->get_subsong(p_index);
	}

	t_filestats input_loop_base::get_file_stats(abort_callback& p_abort) {
		if (m_loopfile.is_valid())
			return merge_filestats(
				       m_loopfile->get_stats(p_abort),
				       m_looptype->get_file_stats(p_abort),
				       merge_filestats_sum);
		else
			return m_looptype->get_file_stats(p_abort);
	}

	void input_loop_base::decode_initialize(t_uint32 p_subsong, unsigned p_flags, abort_callback& p_abort) {
		m_looptype->open_decoding(p_subsong,p_flags,p_abort);
	}

	void input_loop_base::decode_initialize(unsigned p_flags, abort_callback& p_abort) {
		decode_initialize(0,p_flags,p_abort);
	}

	bool input_loop_base::decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
		return m_looptype->run(p_chunk,p_abort);
	}

	bool input_loop_base::decode_run_raw(audio_chunk& p_chunk, mem_block_container& p_raw, abort_callback& p_abort) {
		return m_looptype->run_raw(p_chunk,p_raw,p_abort);
	}

	void input_loop_base::decode_seek(double p_seconds, abort_callback& p_abort) {
		m_looptype->seek(p_seconds,p_abort);
	}

	bool input_loop_base::decode_can_seek() {return m_looptype->can_seek();}

	bool input_loop_base::decode_get_dynamic_info(file_info& p_out, double& p_timestamp_delta) {
		return m_looptype->get_dynamic_info(p_out,p_timestamp_delta);
	}

	bool input_loop_base::decode_get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) {
		return m_looptype->get_dynamic_info_track(p_out, p_timestamp_delta);
	}

	void input_loop_base::decode_on_idle(abort_callback& p_abort) {
		m_looptype->on_idle(p_abort);
	}

	void input_loop_base::retag_set_info(t_uint32 p_subsong, const file_info& p_info, abort_callback& p_abort) {
		throw exception_io_unsupported_format();
	}

	void input_loop_base::retag_commit(abort_callback& p_abort) {
		throw exception_io_unsupported_format();
	}

	void input_loop_base::retag(const file_info& p_info, abort_callback& p_abort) {
		retag_set_info(0,p_info,p_abort);
		retag_commit(p_abort);
	}

	void input_loop_base::set_logger(event_logger::ptr ptr) {
		m_looptype->set_logger(ptr);
	}

	input_loop_base::input_loop_base(const char* p_info_prefix): 
		m_info_prefix(p_info_prefix) {}

	#pragma region GUIDs
	// {E50D5DE0-6F95-4b1c-9165-63D80415ED1B}
	FOOGUIDDECL const GUID loop_type_base::guid_cfg_branch_loop =
	{ 0xe50d5de0, 0x6f95, 0x4b1c, { 0x91, 0x65, 0x63, 0xd8, 0x4, 0x15, 0xed, 0x1b } };
	// {69B1AEBB-E2A6-4c73-AEA2-5037A79B1B62}
	static const GUID guid_cfg_loop_debug =
	{ 0x69b1aebb, 0xe2a6, 0x4c73, { 0xae, 0xa2, 0x50, 0x37, 0xa7, 0x9b, 0x1b, 0x62 } };
	// {9208BA62-AFBE-450e-A468-72792DAE5193}
	static const GUID guid_cfg_loop_disable =
	{ 0x9208ba62, 0xafbe, 0x450e, { 0xa4, 0x68, 0x72, 0x79, 0x2d, 0xae, 0x51, 0x93 } };

	//// {C9E7AF50-FDF8-4a2f-99A6-8DE4D2B49D0C}
	FOOGUIDDECL const GUID loop_type::class_guid = 
	{ 0xc9e7af50, 0xfdf8, 0x4a2f, { 0x99, 0xa6, 0x8d, 0xe4, 0xd2, 0xb4, 0x9d, 0xc } };

	//// {CA8E32C1-1A2D-4679-87AB-03292A97D890}
	FOOGUIDDECL const GUID loop_type_base::class_guid = 
	{ 0xca8e32c1, 0x1a2d, 0x4679, { 0x87, 0xab, 0x3, 0x29, 0x2a, 0x97, 0xd8, 0x90 } };

	//// {D751AD10-1EC1-4711-8698-22ED1C900503}
	//FOOGUIDDECL const GUID loop_type_base_v2::class_guid = 
	//{ 0xd751ad10, 0x1ec1, 0x4711, { 0x86, 0x98, 0x22, 0xed, 0x1c, 0x90, 0x5, 0x3 } };

	//// {2910A6A6-A12B-414f-971B-90A65F79439B}
	FOOGUIDDECL const GUID loop_event_point::class_guid = 
	{ 0x2910a6a6, 0xa12b, 0x414f, { 0x97, 0x1b, 0x90, 0xa6, 0x5f, 0x79, 0x43, 0x9b } };

	//// {566BCC79-7370-48c0-A7CB-5E47C4C17A86}
	FOOGUIDDECL const GUID loop_type_entry::class_guid = 
	{ 0x566bcc79, 0x7370, 0x48c0, { 0xa7, 0xcb, 0x5e, 0x47, 0xc4, 0xc1, 0x7a, 0x86 } };

	//// {399E8435-5341-4549-8C9D-176979EC4300}
	FOOGUIDDECL const GUID loop_type_entry_v2::class_guid = 
	{ 0x399e8435, 0x5341, 0x4549, { 0x8c, 0x9d, 0x17, 0x69, 0x79, 0xec, 0x43, 0x0 } };

	#pragma endregion

	static advconfig_branch_factory cfg_loop_branch("Looping", loop_type_base::guid_cfg_branch_loop, advconfig_branch::guid_branch_decoding, 0);
	advconfig_checkbox_factory cfg_loop_debug("Loop Debugging", guid_cfg_loop_debug, loop_type_base::guid_cfg_branch_loop, 0, false);
	advconfig_checkbox_factory cfg_loop_disable("Loop Disable", guid_cfg_loop_disable, loop_type_base::guid_cfg_branch_loop, 0, false);

}

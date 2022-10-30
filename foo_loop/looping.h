#pragma once

namespace loop_helper {
	extern advconfig_checkbox_factory cfg_loop_debug;
	extern advconfig_checkbox_factory cfg_loop_disable;

	class console_looping_formatter : public console::formatter {
	public:
		console_looping_formatter();
	};

	class console_looping_debug_formatter : public console::formatter {
	public:
		console_looping_debug_formatter();
	};

	//! format time and sample
	class format_samples_ex {
	private:
		pfc::string_fixed_t<193> m_buffer;
	public:
		format_samples_ex(t_uint64 p_samples, t_uint32 p_sample_rate, unsigned p_extra = 3);;
		const char* get_ptr() const;
		operator const char *() const;
	};

	void open_path_helper(input_decoder::ptr & p_input, file::ptr p_file, const char * path,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints);
	bool parse_entity(const char * & ptr,pfc::string8 & name,pfc::string8 & value);
	enum {
		merge_filestats_sum = 1,
		merge_filestats_max = 2,
	};
	t_filestats merge_filestats(const t_filestats & p_src1, const t_filestats & p_src2, int p_merge_type);
	t_filestats2 merge_filestats2(const t_filestats2 & p_src1, const t_filestats2 & p_src2, int p_merge_type);
	template <typename T = input_decoder>
	inline
		t_filestats2 query_input_filestats2(typename T::ptr& p_reader, input_info_reader_v2::ptr& p_reader_v2, t_uint32 s2flags, abort_callback& p_abort)
	{
		return p_reader_v2.is_valid() ? p_reader_v2->get_stats2(s2flags, p_abort) : t_filestats2::from_legacy(p_reader->get_file_stats(p_abort));
	}


	void inline combine_audio_chunks(audio_chunk & p_first,const audio_chunk & p_second) {
		if (p_first.is_empty()) {
			p_first = p_second;
			return;
		}

		// sanity check
		if (p_first.get_sample_rate() != p_second.get_sample_rate() ||
			p_first.get_channel_config() != p_second.get_channel_config() ||
			p_first.get_channels() != p_second.get_channels()) {
			throw exception_unexpected_audio_format_change();
		}
		int nch = p_first.get_channels();
		t_size first_samples = p_first.get_sample_count();
		t_size offset = first_samples * nch;
		t_size second_samples = p_second.get_sample_count();
		t_size size = second_samples * nch;
		p_first.set_data_size(offset + size);
		pfc::memcpy_t(p_first.get_data()+offset,p_second.get_data(),size);
		p_first.set_sample_count(first_samples + second_samples);
	}

	void inline combine_audio_chunks(audio_chunk & p_first,mem_block_container * p_raw_first,const audio_chunk & p_second,const mem_block_container * p_raw_second) {
		if (!p_raw_first != !p_raw_second)
			throw exception_unexpected_audio_format_change();

		combine_audio_chunks(p_first, p_second);

		if (p_raw_first == nullptr) return;

		t_size offset = p_raw_first->get_size();
		t_size size = p_raw_second->get_size();
		// combine mem_block
		p_raw_first->set_size(offset + size);
		memcpy(static_cast<char*>(p_raw_first->get_ptr())+offset,p_raw_second->get_ptr(),size);
	}

	void inline truncate_chunk(audio_chunk & p_chunk, mem_block_container * p_raw, t_size p_samples) {
		if (p_raw != nullptr) p_raw->set_size(MulDiv_Size(p_raw->get_size(), p_samples, p_chunk.get_sample_count()));
		p_chunk.set_sample_count(p_samples);
		p_chunk.set_data_size(p_samples * p_chunk.get_channel_count());
	}

	class NOVTABLE loop_type_base : public service_base {
	protected:
		virtual input_decoder::ptr & get_input() = 0;
		virtual input_decoder_v2::ptr & get_input_v2() = 0;
		virtual void set_succ(bool val) = 0;
		virtual bool get_succ() const = 0;
		virtual void set_cur(t_uint64 val) = 0;
		virtual void add_cur(t_uint64 add) = 0;
	public:
		virtual t_uint64 get_cur() const = 0;
		virtual t_uint32 get_sample_rate() const = 0;
		virtual bool get_no_looping() const = 0;
		virtual void raw_seek(t_uint64 samples, abort_callback& p_abort);
		virtual void raw_seek(double seconds, abort_callback& p_abort);

		void set_eof() {
			set_succ(false);
		}

		void get_one_chunk(audio_chunk & p_chunk, mem_block_container * p_raw, abort_callback & p_abort) {
			if (!get_succ()) return; // already EOF
			if (p_raw != nullptr) {
				set_succ(get_input_v2()->run_raw(p_chunk,*p_raw,p_abort));
			} else {
				set_succ(get_input()->run(p_chunk,p_abort));
			}
			add_cur(p_chunk.get_sample_count());
		}

		virtual t_size get_more_chunk(audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort, t_size candidate);

		static const GUID guid_cfg_branch_loop;
		FB2K_MAKE_SERVICE_INTERFACE(loop_type_base, service_base);
	};

	PFC_DECLARE_EXCEPTION(exception_loop_bad_point,pfc::exception,"Bad Input Loop Point Error")

	class loop_event_point : public service_base {
	public:
		static const t_uint64 position_eof = ~0;

		//! Get position of this event occured.
		virtual t_uint64 get_position() const = 0;

		//! Get position of this event occured with audio chunk includes specified point.
		virtual t_uint64 get_prepare_position() const = 0;

		//! static check of point validity
		virtual void check() const = 0;

		//! Get information of this event.
		virtual void get_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) = 0;

		virtual bool has_dynamic_info() const = 0;
		virtual bool set_dynamic_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) = 0;
		virtual bool reset_dynamic_info(file_info & p_info, const char * p_prefix) = 0;

		virtual bool has_dynamic_track_info() const = 0;
		virtual bool set_dynamic_track_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) = 0;
		virtual bool reset_dynamic_track_info(file_info & p_info, const char * p_prefix) = 0;

		//! process this event with specified chunk. return true after seek or switch input, otherwise false.
		virtual bool process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk & p_chunk, mem_block_container * p_raw, abort_callback & p_abort) = 0;
		//! process this event. return true after seek or switch input, otherwise false.
		virtual bool process(loop_type_base::ptr p_input, abort_callback & p_abort) = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_event_point, service_base);
	};

	template<typename t_item1, typename t_item2>
	int loop_event_compare(const t_item1 & p_item1, const t_item2 & p_item2);

	template<>
	inline int loop_event_compare(const loop_event_point & p_item1, const loop_event_point & p_item2) {
		return pfc::compare_t(p_item1.get_position(), p_item2.get_position());
	}

	template<>
	inline int loop_event_compare(const loop_event_point::ptr & p_item1, const loop_event_point::ptr & p_item2) {
		return pfc::compare_t(p_item1.is_valid() ? p_item1->get_position() : static_cast<t_uint64>(-1), p_item2.is_valid() ? p_item2->get_position() : static_cast<t_uint64>(-1));
	}

	template<>
	inline int loop_event_compare(const t_uint64 & p_item1, const loop_event_point::ptr & p_item2) {
		return pfc::compare_t(p_item1, p_item2.is_valid() ? p_item2->get_position() : static_cast<t_uint64>(-1));
	}

	template<>
	inline int loop_event_compare(const loop_event_point::ptr & p_item1, const t_uint64 & p_item2) {
		return pfc::compare_t(p_item1.is_valid() ? p_item1->get_position() : static_cast<t_uint64>(-1), p_item2);
	}

	template<typename t_item1, typename t_item2>
	int loop_event_prepos_compare(const t_item1 & p_item1, const t_item2 & p_item2);

	template<>
	inline int loop_event_prepos_compare(const loop_event_point & p_item1, const loop_event_point & p_item2) {
		return pfc::compare_t(p_item1.get_prepare_position(), p_item2.get_prepare_position());
	}

	template<>
	inline int loop_event_prepos_compare(const loop_event_point::ptr & p_item1, const loop_event_point::ptr & p_item2) {
		return pfc::compare_t(p_item1.is_valid() ? p_item1->get_prepare_position() : static_cast<t_uint64>(-1), p_item2.is_valid() ? p_item2->get_prepare_position() : static_cast<t_uint64>(-1));
	}

	template<>
	inline int loop_event_prepos_compare(const t_uint64 & p_item1, const loop_event_point::ptr & p_item2) {
		return pfc::compare_t(p_item1, p_item2.is_valid() ? p_item2->get_prepare_position() : static_cast<t_uint64>(-1));
	}

	template<>
	inline int loop_event_prepos_compare(const loop_event_point::ptr & p_item1, const t_uint64 & p_item2) {
		return pfc::compare_t(p_item1.is_valid() ? p_item1->get_prepare_position() : static_cast<t_uint64>(-1), p_item2);
	}

	using loop_event_point_list = pfc::list_t<loop_event_point::ptr, pfc::alloc_fast>;

	class NOVTABLE loop_type : public loop_type_base {
	protected:
		virtual bool is_raw_supported() const = 0;
		virtual const char * get_info_prefix() const = 0;

	public:
		//! parse loop spec file
		virtual bool parse(const char * ptr) = 0;
		//! process specified event with audio chunk and return true after seek or switch input, otherwise false.
		virtual bool process_event(loop_event_point::ptr point, t_uint64 start, audio_chunk & p_chunk, mem_block_container * p_raw, abort_callback & p_abort) = 0;
		//! process specified event and return true after seek or switch input, otherwise false.
		virtual bool process_event(loop_event_point::ptr point, abort_callback & p_abort) = 0;
		virtual bool open_path(file::ptr p_filehint,const char * path,t_input_open_reason p_reason,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) = 0;
		virtual t_uint32 get_subsong_count() = 0;
		virtual t_uint32 get_subsong(t_uint32 p_index) = 0;
		virtual void get_info(t_uint32 subsong, file_info & p_info,abort_callback & p_abort) = 0;
		virtual t_filestats get_file_stats(abort_callback & p_abort) = 0;
		virtual void open_decoding(t_uint32 subsong, t_uint32 flags, abort_callback & p_abort) = 0;
		virtual void seek(t_uint64 samples, abort_callback & p_abort) = 0;
		virtual void seek(double seconds, abort_callback & p_abort) = 0;
		virtual bool run(audio_chunk & p_chunk,abort_callback & p_abort) = 0;
		virtual bool run_raw(audio_chunk & p_chunk, mem_block_container & p_raw, abort_callback & p_abort) = 0;
		virtual void set_info_prefix(const char * p_prefix) = 0;

		virtual void close() = 0;
		bool can_seek() {return get_input()->can_seek();}
		virtual void on_idle(abort_callback & p_abort) = 0;
		virtual bool get_dynamic_info(file_info & p_out,double & p_timestamp_delta) = 0;
		virtual bool get_dynamic_info_track(file_info & p_out,double & p_timestamp_delta) = 0;
		virtual void set_logger(event_logger::ptr ptr) = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_type, loop_type_base);
	};

	class NOVTABLE loop_type_v2 : public loop_type {
	protected:
		virtual input_decoder_v3::ptr & get_input_v3() = 0;
	public:
		//! OPTIONAL, in case your input cares about paused/unpaused state, handle this to do any necessary additional processing. Valid only after initialize() with input_flag_playback.
		virtual void set_pause(bool paused) = 0;
		//! OPTIONAL, should return false in most cases; return true to force playback buffer flush on unpause. Valid only after initialize() with input_flag_playback.
		virtual bool flush_on_pause() = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_type_v2, loop_type);
	};

	class NOVTABLE loop_type_v3 : public loop_type_v2 {
	protected:
		virtual input_decoder_v4::ptr & get_input_v4() = 0;
	public:
		//! OPTIONAL, return 0 if not implemented. \n
		//! Provides means for communication of context specific data with the decoder. The decoder should do nothing and return 0 if it does not recognize the passed arguments.
		virtual size_t extended_param(const GUID & type, size_t arg1, void * arg2, size_t arg2size) = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_type_v3, loop_type_v2);
	};

	class NOVTABLE loop_type_v4 : public loop_type_v3
	{
	protected:
		virtual input_info_reader_v2::ptr & get_info_reader_v2() = 0;
	public:
		virtual t_filestats2 get_stats2(uint32_t s2flags, abort_callback& p_abort) = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_type_v4, loop_type_v3);
	};

	class loop_event_point_baseimpl : public loop_event_point {
	public:
		virtual ~loop_event_point_baseimpl() = default;
		// default: on looping only
		loop_event_point_baseimpl();
		loop_event_point_baseimpl(unsigned p_flags);

		enum {
			on_looping    = 1 << 0,
			on_no_looping = 1 << 1,
		};
		unsigned flags;
		bool check_no_looping(loop_type_base::ptr p_input) const;

		t_uint64 get_prepare_position() const override;
		void get_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		bool has_dynamic_info() const override;
		bool set_dynamic_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		bool reset_dynamic_info(file_info& p_info, const char* p_prefix) override;
		bool has_dynamic_track_info() const override;
		bool set_dynamic_track_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		bool reset_dynamic_track_info(file_info& p_info, const char* p_prefix) override;
	};

	class loop_event_point_simple : public loop_event_point_baseimpl {
	public:
		// this event process on looping only, default
		loop_event_point_simple();
		t_uint64 from, to;
		t_size maxrepeats, repeats;
		t_uint64 get_position() const override;
		void get_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		bool has_dynamic_info() const override;
		bool set_dynamic_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		bool reset_dynamic_info(file_info& p_info, const char* p_prefix) override;
		void check() const override;
		bool process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) override;
		bool process(loop_type_base::ptr p_input, abort_callback& p_abort) override;
	};

	class loop_event_point_end : public loop_event_point_baseimpl {
	public:
		// this event process on no_looping only, default
		loop_event_point_end();
		t_uint64 position;
		t_uint64 get_position() const override;
		void get_info(file_info& p_info, const char* p_prefix, t_uint32 sample_rate) override;
		void check() const override;
		bool process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) override;
		bool process(loop_type_base::ptr p_input, abort_callback& /*p_abort*/) override;
	};

	class loop_type_impl_base : public loop_type_v4 {
	private:
		t_uint32 m_sample_rate;
		t_uint64 m_cur;
		bool m_no_looping;
		bool m_succ;
		bool m_raw_support;
		pfc::string8 m_info_prefix;
		loop_event_point_list m_cur_points;
		pfc::list_permutation_t<loop_event_point::ptr> * m_cur_points_by_pos, * m_cur_points_by_prepos;
		pfc::array_t<t_size> m_perm_by_pos, m_perm_by_prepos;
		input_decoder::ptr m_current_input;
		input_decoder_v2::ptr m_current_input_v2;
		input_decoder_v3::ptr m_current_input_v3;
		input_decoder_v4::ptr m_current_input_v4;
		input_info_reader_v2::ptr m_current_info_reader_v2;
		event_logger::ptr m_logger;
		bool m_current_changed;
		pfc::string8 m_current_path, m_current_fileext;
		t_uint64 m_nextpointpos;
		class dynamic_update_tracker {
		public:
			t_uint64 lastupdate;
			t_uint64 updateperiod;
			loop_event_point_list m_old_points;
			bool m_input_switched;
			double m_input_switched_pos;

			dynamic_update_tracker();

			bool check(t_uint64 cur) const {
				return lastupdate >= cur || (lastupdate + updateperiod) < cur;
			}

			bool check_and_update(t_uint64 cur) {
				if (check(cur)) {
					lastupdate = cur;
					return true;
				}
				return false;
			}
		};
		dynamic_update_tracker m_dynamic, m_dynamic_track;

		t_size get_prepare_length(t_uint64 p_start, t_uint64 p_end, t_size nums) {
			t_size n;
			bsearch_points_by_prepos(p_start, n);
			auto maxpos = p_end;
			while (n < nums) {
				const auto point = get_points_by_prepos()[n];
				if (point->get_prepare_position() > p_end) break;
				maxpos = pfc::max_t<t_uint64>(maxpos, point->get_position());
				n++;
			}
			return pfc::downcast_guarded<t_size>(maxpos - p_end);
		}

		t_uint64 get_prepare_pos(t_uint64 p_pos, t_size nums) {
			t_size n;
			bsearch_points_by_prepos(p_pos, n);
			if (n < nums) {
				return get_points_by_prepos()[n]->get_prepare_position();
			}
			return static_cast<t_uint64>(-1);
		}

	protected:
		bool get_succ() const override {return m_succ;}
		void set_succ(bool val) override {m_succ = val;}
		void set_cur(t_uint64 val) override { m_cur = val; }
		void add_cur(t_uint64 add) override { m_cur += add; }
		bool is_raw_supported() const override {return m_raw_support;}
		virtual double get_dynamic_updateperiod() const;
		virtual double get_dynamictrack_updateperiod() const;
		virtual void set_dynamic_updateperiod(double p_time);
		virtual void set_dynamictrack_updateperiod(double p_time);
		const char * get_info_prefix() const override {return m_info_prefix;}

		//! open file. please call switch_input.
		virtual bool open_path_internal(file::ptr p_filehint,const char * path,t_input_open_reason p_reason,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) = 0;
		//! open decoding. please call switch_points in this or open_path_internal.
		virtual void open_decoding_internal(t_uint32 subsong, t_uint32 flags, abort_callback & p_abort) = 0;

		input_decoder::ptr& get_input() override;
		input_decoder_v2::ptr& get_input_v2() override;
		input_decoder_v3::ptr& get_input_v3() override;
		input_decoder_v4::ptr& get_input_v4() override;
		input_info_reader_v2::ptr& get_info_reader_v2() override;

		virtual __declspec(deprecated) void switch_input(input_decoder::ptr p_input);
		virtual __declspec(deprecated) void switch_input(input_decoder::ptr p_input, const char* p_path);
		virtual void switch_input(input_decoder::ptr p_input, const char* p_path, double pos_on_decode);
		virtual void switch_points(loop_event_point_list p_list);

		virtual pfc::list_permutation_t<loop_event_point::ptr> get_points_by_pos();

		virtual pfc::list_permutation_t<loop_event_point::ptr> get_points_by_prepos();

		loop_event_point_list get_points() {
			return m_cur_points;
		}

		virtual t_size bsearch_points_by_pos(t_uint64 pos, t_size& index);
		virtual t_size bsearch_points_by_prepos(t_uint64 pos, t_size& index);

		void set_is_raw_supported(bool val) {m_raw_support = val;}
		void set_no_looping(bool val) {m_no_looping = val;}

		virtual t_size get_nearest_point(t_uint64 pos);

		void do_current_events(abort_callback & p_abort) {
			do_events(get_cur(), p_abort);
		}

		void do_events(t_uint64 p_pos, abort_callback & p_abort) {
			do_events(p_pos, p_pos, p_abort);
		}

		virtual void do_events(t_uint64 p_start, t_uint64 p_end, abort_callback& p_abort);

		virtual void do_events(t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort);

		void user_seek(double seconds, abort_callback & p_abort) {
			raw_seek(seconds, p_abort);
			do_current_events(p_abort);
		}

		void user_seek(t_uint64 samples, abort_callback & p_abort) {
			raw_seek(samples, p_abort);
			do_current_events(p_abort);
		}

		virtual bool set_dynamic_info(file_info& p_out);

		//! called after switch_points or switch_input
		virtual bool reset_dynamic_info(file_info& p_out);
		virtual bool set_dynamic_info_track(file_info& p_out);

		//! called after switch_points or switch_input
		virtual bool reset_dynamic_info_track(file_info& p_out);

		virtual bool run_common(audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort);

		static void get_info_for_points(file_info& p_info, loop_event_point_list& points, const char* p_prefix, t_uint32 p_sample_rate);

		class dispatch_dynamic_info {
		public:
			static bool point_check(loop_event_point::ptr point) {
				return point->has_dynamic_info();
			}

			static bool point_set(loop_event_point::ptr point, file_info & p_info, const char * p_prefix, t_uint32 p_sample_rate) {
				return point->set_dynamic_info(p_info, p_prefix, p_sample_rate);
			}

			static bool point_reset(loop_event_point::ptr point, file_info & p_info, const char * p_prefix) {
				return point->reset_dynamic_info(p_info, p_prefix);
			}

			static bool parent_get(input_decoder::ptr & parent, file_info & p_out, double & p_timestamp_delta) {
				return parent->get_dynamic_info(p_out, p_timestamp_delta);
			}

			static bool self_set(loop_type_impl_base & impl, file_info & p_out) {
				return impl.set_dynamic_info(p_out);
			}

			static bool self_reset(loop_type_impl_base & impl, file_info & p_out) {
				return impl.reset_dynamic_info(p_out);
			}
		};

		class dispatch_dynamic_track_info {
		public:
			static bool point_check(loop_event_point::ptr point) {
				return point->has_dynamic_track_info();
			}

			static bool point_set(loop_event_point::ptr point, file_info & p_info, const char * p_prefix, t_uint32 p_sample_rate) {
				return point->set_dynamic_track_info(p_info, p_prefix, p_sample_rate);
			}

			static bool point_reset(loop_event_point::ptr point, file_info & p_info, const char * p_prefix) {
				return point->reset_dynamic_track_info(p_info, p_prefix);
			}

			static bool parent_get(input_decoder::ptr & parent, file_info & p_out, double & p_timestamp_delta) {
				return parent->get_dynamic_info_track(p_out, p_timestamp_delta);
			}

			static bool self_set(loop_type_impl_base & impl, file_info & p_out) {
				return impl.set_dynamic_info_track(p_out);
			}

			static bool self_reset(loop_type_impl_base & impl, file_info & p_out) {
				return impl.reset_dynamic_info_track(p_out);
			}
		};

		template <typename t_dispatcher>
		bool get_dynamic_info_t(file_info & p_out,double & p_timestamp_delta, dynamic_update_tracker & tracker) {
			bool ret = t_dispatcher::parent_get(get_input(),p_out,p_timestamp_delta);
			auto& oldlist = tracker.m_old_points;
			if (oldlist.get_count() != 0 || tracker.m_input_switched) {
				ret |= t_dispatcher::self_reset(*this, p_out);
				for (t_size n = 0, m = get_points().get_count(); n < m; ++n ) {
					auto point = get_points()[n];
					if (t_dispatcher::point_check(point)) {
						pfc::string8 name;
						name << get_info_prefix() << "point_" << pfc::format_int(n, 2) << "_";
						ret |= t_dispatcher::point_reset(point, p_out, name);
					}
				}
				oldlist.remove_all();
				tracker.m_input_switched = false;
				if (ret && tracker.m_input_switched_pos >= 0) {
					p_timestamp_delta = pfc::max_t(p_timestamp_delta, tracker.m_input_switched_pos);
					tracker.m_input_switched_pos = -1;
				}
			}
			if (!get_no_looping()) {
				if (tracker.check_and_update(get_cur())) {
					ret |= t_dispatcher::self_set(*this, p_out);
					auto sample_rate = get_sample_rate();
					for (t_size n = 0, m = get_points().get_count(); n < m; ++n ) {
						auto point = get_points()[n];
						if (t_dispatcher::point_check(point)) {
							pfc::string8 name;
							name << get_info_prefix() << "point_" << pfc::format_int(n, 2) << "_";
							ret |= t_dispatcher::point_set(point, p_out, name, sample_rate);
						}
					}
				}
			}
			return ret;
		}

	public:
		loop_type_impl_base();
		virtual ~loop_type_impl_base();

		t_uint64 get_cur() const override;
		t_uint32 get_sample_rate() const override;
		bool get_no_looping() const override;
		void set_info_prefix(const char* p_prefix) override;

		bool open_path(file::ptr p_filehint, const char* path, t_input_open_reason p_reason, abort_callback& p_abort, bool p_from_redirect, bool p_skip_hints) override;
		void open_decoding(t_uint32 subsong, t_uint32 flags, abort_callback& p_abort) override;
		bool process_event(loop_event_point::ptr point, t_uint64 p_start, audio_chunk& p_chunk, mem_block_container* p_raw, abort_callback& p_abort) override;
		bool process_event(loop_event_point::ptr point, abort_callback& p_abort) override;

		bool run(audio_chunk& p_chunk, abort_callback& p_abort) override;
		bool run_raw(audio_chunk& p_chunk, mem_block_container& p_raw, abort_callback& p_abort) override;

		void seek(double p_seconds, abort_callback& p_abort) override;
		void seek(t_uint64 p_samples, abort_callback& p_abort) override;

		// other input_decoder methods
		bool get_dynamic_info(file_info& p_out, double& p_timestamp_delta) override;
		bool get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) override;

		void set_logger(event_logger::ptr ptr) override;

		//! OPTIONAL, in case your input cares about paused/unpaused state, handle this to do any necessary additional processing. Valid only after initialize() with input_flag_playback.
		void set_pause(bool paused) override;
		//! OPTIONAL, should return false in most cases; return true to force playback buffer flush on unpause. Valid only after initialize() with input_flag_playback.
		bool flush_on_pause() override;

		//! OPTIONAL, return 0 if not implemented. \n
		//! Provides means for communication of context specific data with the decoder. The decoder should do nothing and return 0 if it does not recognize the passed arguments.
		size_t extended_param(const GUID & type, size_t arg1, void * arg2, size_t arg2size) override;
	};

	class loop_type_impl_singleinput_base : public loop_type_impl_base {
	protected:
		void open_decoding_internal(t_uint32 subsong, t_uint32 flags, abort_callback& p_abort) override;

	public:
		t_uint32 get_subsong_count() override;
		t_uint32 get_subsong(t_uint32 p_index) override;

		void get_info(t_uint32 subsong, file_info& p_info, abort_callback& p_abort) override;
		t_filestats get_file_stats(abort_callback& p_abort) override;
		t_filestats2 get_stats2(uint32_t s2flags, abort_callback& p_abort) override;

		void close() override;
		void on_idle(abort_callback& p_abort) override;
	};

	class loop_type_entry : public service_base {
	public:
		virtual const char * get_name() const = 0;
		virtual const char * get_short_name() const = 0;
		virtual bool is_our_type(const char * type) const = 0;
		virtual bool is_explicit() const = 0;
		virtual loop_type::ptr instantiate() const = 0;

		FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(loop_type_entry);
	};

	template<typename t_instance_impl>
	class loop_type_impl_t : public loop_type_entry {
	public:
		const char * get_name() const override {return t_instance_impl::g_get_name();}
		const char * get_short_name() const override {return t_instance_impl::g_get_short_name();}
		bool is_our_type(const char * type) const override { return t_instance_impl::g_is_our_type(type); }
		bool is_explicit() const override { return t_instance_impl::g_is_explicit(); }
		loop_type::ptr instantiate() const override {return new service_impl_t<t_instance_impl>();}
	};

	template<typename t_instance_impl> class loop_type_factory_t :
		public service_factory_single_t<loop_type_impl_t<t_instance_impl> > {};

	class loop_type_entry_v2 : public loop_type_entry {
	public:
		//! default priority = 100, lower equals faster
		virtual t_uint8 get_priority() const = 0;

		FB2K_MAKE_SERVICE_INTERFACE(loop_type_entry_v2, loop_type_entry);
	};

	template<typename t_instance_impl>
	class loop_type_impl_v2_t : public loop_type_entry_v2  {
	public:
		const char * get_name() const override {return t_instance_impl::g_get_name();}
		const char * get_short_name() const override {return t_instance_impl::g_get_short_name();}
		bool is_our_type(const char * type) const override {return t_instance_impl::g_is_our_type(type);}
		bool is_explicit() const override {return t_instance_impl::g_is_explicit();}
		loop_type::ptr instantiate() const override {return new service_impl_t<t_instance_impl>();}
		t_uint8 get_priority() const override {return t_instance_impl::g_get_priority();}
	};

	template<typename t_instance_impl> class loop_type_factory_v2_t :
		public service_factory_single_t<loop_type_impl_v2_t<t_instance_impl> > {};

	class loop_type_none : public loop_type_impl_singleinput_base
	{
	private:
		input_decoder::ptr m_input;
		loop_event_point_list m_points;
	public:
		static const char* g_get_name();
		static const char* g_get_short_name();
		static bool g_is_our_type(const char* type);
		static bool g_is_explicit();
		bool parse(const char* ptr) override;
		bool open_path_internal(file::ptr p_filehint, const char* path, t_input_open_reason p_reason, abort_callback& p_abort, bool p_from_redirect, bool p_skip_hints) override;
	};

	class loop_type_entire : public loop_type_impl_singleinput_base
	{
	private:
		input_decoder::ptr m_input;
		loop_event_point_list m_points;
	public:
		static const char* g_get_name();
		static const char* g_get_short_name();
		static bool g_is_our_type(const char* type);
		static bool g_is_explicit();
		bool parse(const char* ptr) override;
		bool open_path_internal(file::ptr p_filehint, const char * path, t_input_open_reason p_reason, abort_callback & p_abort, bool p_from_redirect, bool p_skip_hints) override;
	};

	class NOVTABLE input_loop_base
	{
	public:
		void open(file::ptr p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort);

		void get_info(t_uint32 p_subsong, file_info& p_info, abort_callback& p_abort);
		void get_info(file_info& p_info, abort_callback& p_abort);
		t_uint32 get_subsong_count();
		t_uint32 get_subsong(t_uint32 p_index);
		t_filestats get_file_stats(abort_callback& p_abort);
		t_filestats2 get_stats2(uint32_t s2flags, abort_callback& p_abort);
		void decode_initialize(t_uint32 p_subsong, unsigned p_flags, abort_callback& p_abort);
		void decode_initialize(unsigned p_flags, abort_callback& p_abort);
		bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort);
		bool decode_run_raw(audio_chunk& p_chunk, mem_block_container& p_raw, abort_callback& p_abort);
		void decode_seek(double p_seconds, abort_callback& p_abort);
		bool decode_can_seek();
		bool decode_get_dynamic_info(file_info& p_out, double& p_timestamp_delta);
		bool decode_get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta);
		void decode_on_idle(abort_callback& p_abort);
		void retag_set_info(t_uint32 p_subsong, const file_info& p_info, abort_callback& p_abort);
		void retag_commit(abort_callback& p_abort);
		void retag(const file_info& p_info, abort_callback& p_abort);
		void remove_tags(abort_callback & abort);
		void set_logger(event_logger::ptr ptr);
		void set_pause(bool paused);
		bool flush_on_pause();
		size_t extended_param(const GUID & type, size_t arg1, void * arg2, size_t arg2size);

		using interface_decoder_t = input_decoder_v4;
		using interface_info_reader_t = input_info_reader_v2;
		using interface_info_writer_t = input_info_writer_v2;

	protected:
		input_loop_base(const char* p_info_prefix);
		// Please call switch_loop
		virtual void open_internal(file::ptr p_filehint,const char * p_path,t_input_open_reason p_reason,abort_callback & p_abort) = 0;
		void set_looptype(loop_type::ptr looptype);
		//static bool g_is_our_content_type(const char * p_content_type);
		//static bool g_is_our_path(const char * p_path,const char * p_extension);
		file::ptr m_loopfile;
		file_v2::ptr m_loopfile_v2;
		pfc::string8 m_path;
		loop_type_entry::ptr m_loopentry;
		loop_type::ptr m_looptype;
		loop_type_v2::ptr m_looptype_v2;
		loop_type_v3::ptr m_looptype_v3;
		loop_type_v4::ptr m_looptype_v4;
		event_logger::ptr m_logger;
		pfc::string8 m_loopcontent;
		pfc::string8 m_info_prefix;
	};

	void do_crossfade(audio_sample* p_dest, const audio_sample* p_src1, const audio_sample* p_src2,
	                  int nch, t_size samples, t_uint ratiostart, t_uint ratioend);

	void do_crossfade(audio_chunk& p_dest, t_size destpos, const audio_chunk& p_src1, t_size src1pos,
	                  const audio_chunk& p_src2, t_size src2pos, t_size samples, t_uint ratiostart, t_uint ratioend);

	void do_crossfade(audio_chunk& p_dest, t_size destpos, const audio_chunk& p_src, t_size srcpos,
	                  t_size samples, t_uint ratiostart, t_uint ratioend);

	inline int isspace(char c) {
		return ::isspace(static_cast<unsigned char>(c));
	}

	//// {566BCC79-7370-48c0-A7CB-5E47C4C17A86}
	FOOGUIDDECL const GUID loop_type_entry::class_guid =
	{ 0x566bcc79, 0x7370, 0x48c0,{ 0xa7, 0xcb, 0x5e, 0x47, 0xc4, 0xc1, 0x7a, 0x86 } };

	//// {399E8435-5341-4549-8C9D-176979EC4300}
	FOOGUIDDECL const GUID loop_type_entry_v2::class_guid =
	{ 0x399e8435, 0x5341, 0x4549,{ 0x8c, 0x9d, 0x17, 0x69, 0x79, 0xec, 0x43, 0x0 } };

	//// {C9E7AF50-FDF8-4a2f-99A6-8DE4D2B49D0C}
	FOOGUIDDECL const GUID loop_type::class_guid =
	{ 0xc9e7af50, 0xfdf8, 0x4a2f,{ 0x99, 0xa6, 0x8d, 0xe4, 0xd2, 0xb4, 0x9d, 0xc } };

	// {EE404A04-7B81-4D93-B477-96855A54D864}
	FOOGUIDDECL const GUID loop_type_v2::class_guid =
	{ 0xee404a04, 0x7b81, 0x4d93,{ 0xb4, 0x77, 0x96, 0x85, 0x5a, 0x54, 0xd8, 0x64 } };

	// {E5F03B86-CAEF-4B3E-B150-45FA567A5C8E}
	FOOGUIDDECL const GUID loop_type_v3::class_guid =
	{ 0xe5f03b86, 0xcaef, 0x4b3e,{ 0xb1, 0x50, 0x45, 0xfa, 0x56, 0x7a, 0x5c, 0x8e } };

	// {C8F317A5-93E7-4F7C-8CC8-284BD50CF6B9}
	FOOGUIDDECL const GUID loop_type_v4::class_guid =
	{ 0xc8f317a5, 0x93e7, 0x4f7c,{ 0x8c, 0xc8, 0x28, 0x4b, 0xd5, 0xc, 0xf6, 0xb9 } };

	//// {CA8E32C1-1A2D-4679-87AB-03292A97D890}
	FOOGUIDDECL const GUID loop_type_base::class_guid =
	{ 0xca8e32c1, 0x1a2d, 0x4679,{ 0x87, 0xab, 0x3, 0x29, 0x2a, 0x97, 0xd8, 0x90 } };
}

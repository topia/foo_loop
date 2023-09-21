// foo_sli.cpp : Defines the exported functions for the DLL application.
//

// Licence: GPL v2 (check kirikiri 2 SDK License)

/*
see also:
	* http://devdoc.kikyou.info/tvp/docs/kr2doc/contents/LoopTuner.html
	* https://sv.kikyou.info/trac/kirikiri/browser/kirikiri2/trunk/license.txt
	* https://sv.kikyou.info/trac/kirikiri/browser/kirikiri2/trunk/kirikiri2/src/core/sound/WaveLoopManager.h
	* https://sv.kikyou.info/trac/kirikiri/browser/kirikiri2/trunk/kirikiri2/src/core/sound/WaveLoopManager.cpp
	* https://sv.kikyou.info/trac/kirikiri/browser/kirikiri2/trunk/kirikiri2/src/core/sound/WaveSegmentQueue.h
	* https://sv.kikyou.info/trac/kirikiri/browser/kirikiri2/trunk/kirikiri2/src/core/sound/WaveSegmentQueue.cpp
*/

#include "stdafx.h"
#include "looping.h"
#include <functional>
#include <utility>

using namespace loop_helper;

class sli_link : public loop_event_point_baseimpl {
public:
	//! set smooth sample count. return true if this link use smoothing, otherwise false.
	virtual bool set_smooth_samples(t_size samples) = 0;
	FB2K_MAKE_SERVICE_INTERFACE(sli_link, loop_event_point);
};

template <typename trait_t>
struct value_clipper {
	static typename trait_t::value_t clip_both(typename trait_t::value_t value) {
		return pfc::clip_t<typename trait_t::value_t>(value, trait_t::min_value, trait_t::max_value);
	}
	static typename trait_t::value_t clip_min(typename trait_t::value_t value) {
		return pfc::max_t<typename trait_t::value_t>(value, trait_t::min_value);
	}
	static typename trait_t::value_t clip_max(typename trait_t::value_t value) {
		return pfc::min_t<typename trait_t::value_t>(value, trait_t::max_value);
	}
};

// {808359F5-36A6-4b67-BF0D-5FA83C404035}
static constexpr GUID guid_cfg_sli_label_log = 
{ 0x808359f5, 0x36a6, 0x4b67, { 0xbf, 0xd, 0x5f, 0xa8, 0x3c, 0x40, 0x40, 0x35 } };
advconfig_checkbox_factory cfg_sli_label_logging("SLI Label Logging",guid_cfg_sli_label_log,loop_type_base::guid_cfg_branch_loop,0,false);

template <typename sli_value_t, int sli_num_flags, sli_value_t sli_min_value, sli_value_t sli_max_value, typename loop_type_sli_t>
struct sli_processor {
	using value_t = sli_value_t;
	enum {
		num_flags = sli_num_flags,
		min_value = sli_min_value,
		max_value = sli_max_value,
	};
	struct flag_trait_t {
		using value_t = sli_value_t;
		enum {
			min_value = 0,
			max_value = sli_num_flags + 1,
		};
	};
	using flag_clipper = value_clipper<flag_trait_t>;
	struct value_trait_t {
		using value_t = sli_value_t;
		enum {
			min_value = sli_min_value,
			max_value = sli_max_value,
		};
	};
	using value_clipper = value_clipper<value_trait_t>;

	class loop_condition {
	public:
		loop_condition(const char * confname, const char * symbol, bool is_valid) :
			confname(confname), symbol(symbol), is_valid(is_valid) {}
		virtual ~loop_condition() = default;

		const char * const confname;
		const char * const symbol;
		const bool is_valid;
		virtual bool check(value_t a, value_t b) const = 0;
	};

	class loop_condition_impl : public loop_condition {
	public:
		std::function<bool(sli_value_t, sli_value_t)> oper;

		loop_condition_impl(const char * confname, const char * symbol, bool is_valid, std::function<bool(value_t, value_t)> oper) :
			loop_condition(confname, symbol, is_valid), oper(std::move(oper)) {}

		virtual ~loop_condition_impl() = default;

		bool check(value_t a, value_t b) const override {
			return oper(a, b);
		}
	};

	struct loop_conditions {
		loop_condition** conds;

		loop_conditions() {
			struct condition_spec {
				const char* confname;
				const char* symbol;
				bool is_valid;
				std::function<bool(value_t a, value_t b)> oper;
			};
			struct condition_spec specs[] = {
				{ "no", nullptr, false, [](value_t, value_t) -> bool { return true; } },
#define DEFINE_LOOP_CONDITION(name, op) \
				{ #name, #op, true, [](value_t a, value_t b) -> bool { return a op b; }}
				DEFINE_LOOP_CONDITION(eq, ==),
				DEFINE_LOOP_CONDITION(ne, != ),
				DEFINE_LOOP_CONDITION(gt, >),
				DEFINE_LOOP_CONDITION(ge, >= ),
				DEFINE_LOOP_CONDITION(lt, <),
				DEFINE_LOOP_CONDITION(le, <= ),
#undef DEFINE_LOOP_CONDITION
			};

			const auto num = tabsize(specs);
			conds = new loop_condition*[num + 1];
			for (size_t i = 0; i < num; ++i) {
				conds[i] = new loop_condition_impl(specs[i].confname, specs[i].symbol, specs[i].is_valid, specs[i].oper);
			}
			conds[num] = nullptr;
		}
		~loop_conditions() {
			delete[] conds;
		}

		loop_condition const* parse(const char* value) const {
			for (loop_condition **ptr = conds; *ptr != nullptr; ++ptr) {
				if (!pfc::stricmp_ascii((*ptr)->confname, value)) return *ptr;
			}
			return nullptr;
		}
	};

	class formula_operator {
	public:
		const char * const symbol;
		const bool require_operand;
		formula_operator(const char * symbol, const bool require_operand) :
			symbol(symbol), require_operand(require_operand) {}
		virtual ~formula_operator() = default;

		virtual value_t calculate(value_t original, value_t operand) const = 0;
	};

#define DEFINE_FORMULA_OP(name, symbol, value, clipper) \
	class formula_operator_ ##name : public formula_operator { \
	public: \
  	formula_operator_ ##name() : formula_operator(#symbol, true) {}; \
	virtual value_t calculate(value_t original, value_t operand) const { \
	return value_clipper::clip_ ##clipper(value); \
	} \
	};

	DEFINE_FORMULA_OP(set, = , operand, both);
	DEFINE_FORMULA_OP(add, +=, original + operand, max);
	DEFINE_FORMULA_OP(sub, -=, original - operand, min);
	DEFINE_FORMULA_OP(inc, ++, original + 1, max);
	DEFINE_FORMULA_OP(dec, --, original - 1, min);
#undef DEFINE_FORMULA_OP

	class link_impl : public sli_link {
	private:
		t_size smooth_samples;
	protected:
		link_impl() : smooth_samples(0), from(0), to(0), smooth(false), condition(nullptr), refvalue(0), condvar(0),
			seens(0) {}
		~link_impl() = default;
	public:
		t_uint64 get_position() const override { return from; }
		t_uint64 get_prepare_position() const override { return from - smooth_samples; }

		bool set_smooth_samples(t_size samples) override {
			if (!smooth) return false;
			smooth_samples = static_cast<t_size>(pfc::min_t(static_cast<t_uint64>(samples), pfc::min_t(from, to)));
			return true;
		}
		t_uint64 from;
		t_uint64 to;
		bool smooth;
		loop_condition const* condition;
		typename value_trait_t::value_t refvalue;
		typename flag_trait_t::value_t condvar;
		t_size seens;

		void get_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) override {
			pfc::string8 name, buf;
			name << p_prefix;
			const auto prefixlen = name.get_length();

			name.truncate(prefixlen);
			name << "from";
			p_info.info_set(name, format_samples_ex(from, sample_rate));

			name.truncate(prefixlen);
			name << "to";
			p_info.info_set(name, format_samples_ex(to, sample_rate));

			name.truncate(prefixlen);
			name << "type";
			buf << "link";
			if (smooth) {
				buf << "; smooth";
			}
			if (condition != nullptr && condition->is_valid) {
				buf << "; cond:";
				buf << "[" << condvar << "]";
				buf << condition->symbol << refvalue;
			}

			p_info.info_set(name, buf);
		}

		bool has_dynamic_info() const override { return true; }

		bool set_dynamic_info(file_info & p_info, const char * p_prefix, t_uint32 /*sample_rate*/) override {
			pfc::string8 name;
			name << p_prefix << "seens";
			p_info.info_set_int(name, seens);
			return true;
		}

		bool reset_dynamic_info(file_info & p_info, const char * p_prefix) override {
			pfc::string8 name;
			name << p_prefix << "seens";
			return p_info.info_remove(name);
		}

		void check() const override {
			if (from == to) throw exception_loop_bad_point();
		}
		virtual bool check_condition(loop_type_base::ptr p_input) {
			typename loop_type_sli_t::ptr p_input_special;
			if (p_input->service_query_t<loop_type_sli_t>(p_input_special)) {
				return p_input_special->check_condition(*this);
			}
			return true;
		}

		bool process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk & p_chunk, mem_block_container * p_raw, abort_callback & p_abort) override {
			// this event do not process on no_looping
			if ((p_input->get_no_looping() && from >= to) || !check_condition(p_input)) return false;
			++seens;
			const auto point = pfc::downcast_guarded<t_size>(from - p_start);
			typename loop_type_sli_t::ptr p_input_special;
			if (!smooth || !p_input->service_query_t<loop_type_sli_t>(p_input_special)) {
				truncate_chunk(p_chunk, p_raw, point);
				p_input->raw_seek(to, p_abort);
				return true;
			}
			// smooth
			PFC_ASSERT(p_raw == nullptr); // we do not support raw streaming with smoothing
			const auto smooth_first_samples = smooth_samples;
			const t_size require_samples = point + p_input_special->get_crossfade_samples_half();
			auto samples = p_chunk.get_sample_count();
			if (require_samples > samples)
				samples += p_input->get_more_chunk(p_chunk, p_raw, p_abort, require_samples - samples);
			if (require_samples < samples)
				truncate_chunk(p_chunk, p_raw, require_samples);// only debug mode ?
			auto smooth_latter_samples = pfc::min_t(require_samples, samples) - point;
			// seek
			p_input->raw_seek(to - smooth_first_samples, p_abort);
			// get new samples
			audio_chunk_impl_temporary ptmp_chunk;
			samples = p_input->get_more_chunk(ptmp_chunk, nullptr, p_abort, smooth_first_samples + smooth_latter_samples);
			smooth_latter_samples = pfc::min_t(samples - smooth_first_samples, smooth_latter_samples);
			// crossfading
			loop_helper::do_crossfade(
				p_chunk, point - smooth_first_samples,
				ptmp_chunk, 0,
				smooth_first_samples, 0, 50);
			loop_helper::do_crossfade(
				p_chunk, point,
				ptmp_chunk, smooth_first_samples,
				smooth_latter_samples, 50, 100);
			p_chunk.set_sample_count(point + smooth_latter_samples);
			const auto smooth_total_samples = smooth_first_samples + smooth_latter_samples;
			const auto latter = audio_chunk_partial_ref(
				ptmp_chunk, smooth_total_samples, samples - smooth_total_samples);
			combine_audio_chunks(p_chunk, latter);
			return true;
		}

		bool process(loop_type_base::ptr p_input, abort_callback & p_abort) override {
			// this event do not process on no_looping
			if (p_input->get_no_looping() || !check_condition(p_input)) return false;
			p_input->raw_seek(to, p_abort);
			++seens;
			return true;
		}
	};

	class label : public loop_event_point_baseimpl {
	public:
		label() : loop_event_point_baseimpl(on_looping | on_no_looping), position(0), seens(0) {}
		t_uint64 get_position() const override { return position; }
		t_uint64 get_prepare_position() const override { return position; }
		t_uint64 position;
		t_size seens;
		pfc::string8 name;
		void check() const override {}

		void get_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) override {
			pfc::string8 info_name;
			info_name << p_prefix;
			const auto prefixlen = info_name.get_length();

			info_name.truncate(prefixlen);
			info_name << "type";
			p_info.info_set(info_name, "label");

			info_name.truncate(prefixlen);
			info_name << "pos";
			p_info.info_set(info_name, format_samples_ex(position, sample_rate));

			if (this->name) {
				if (this->name[0] == ':') {
					info_name.truncate(prefixlen);
					info_name << "formula";
					p_info.info_set(info_name, this->name.get_ptr() + 1);
				}
				else {
					info_name.truncate(prefixlen);
					info_name << "name";
					p_info.info_set(info_name, this->name);
				}
			}
		}

		bool has_dynamic_info() const override { return true; }

		bool set_dynamic_info(file_info & p_info, const char * p_prefix, t_uint32 /*sample_rate*/) override {
			pfc::string8 info_name;
			info_name << p_prefix << "seens";
			p_info.info_set_int(info_name, seens);
			return true;
		}

		bool reset_dynamic_info(file_info & p_info, const char * p_prefix) override {
			pfc::string8 info_name;
			info_name << p_prefix << "seens";
			return p_info.info_remove(info_name);
		}

		bool process(loop_type_base::ptr p_input, t_uint64 /*p_start*/, audio_chunk & /*p_chunk*/, mem_block_container * /*p_raw*/, abort_callback & p_abort) override {
			return process(p_input, p_abort);
		}

		bool process(loop_type_base::ptr p_input, abort_callback & /*p_abort*/) override {
			// this event do not process on no_looping
			if (cfg_sli_label_logging.get()) {
				console::formatter() << "SLI: Label: " << name << " at " <<
					format_samples_ex(get_position(), p_input->get_sample_rate());
			}
			++seens;
			if (name[0] != ':') return false;
			typename loop_type_sli_t::ptr p_input_special;
			if (p_input->service_query_t<loop_type_sli_t>(p_input_special)) {
				p_input_special->process_formula(name.get_ptr() + 1);
			}
			return false;
		}
	};

	class label_formula {
	public:
		label_formula() : oper(nullptr), indirect(false) {}
		~label_formula() {
			delete oper;
		}
		value_t flag;
		formula_operator const * oper;
		bool indirect;
		value_t value;
	};

	static bool parse_entity(const char * & ptr, pfc::string8 & name, pfc::string8 & value) {
		auto delimiter = '\0';
		char tmp;
		t_size n = 0;
		while (isspace(*ptr)) ptr++;
		while (tmp = ptr[n], tmp && !isspace(tmp) && tmp != '=') n++;
		if (!ptr[n]) return false;
		name.set_string(ptr, n);
		ptr += n;
		while (isspace(*ptr)) ptr++;
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
			while (tmp = ptr[n], tmp && !isspace(tmp) && tmp != ';') n++;
		}
		else {
			while (tmp = ptr[n], tmp && tmp != delimiter) n++;
		}
		if (!ptr[n]) return false;
		value.set_string(ptr, n);
		ptr += n;
		if (*ptr == delimiter) ptr++;
		while (*ptr == ';' || isspace(*ptr)) ptr++;
		return true;
	}

	static bool parse_link(const char * & ptr, const loop_conditions& conds, service_ptr_t<link_impl> link) {
		// must point '{' , which indicates start of the block.
		if (*ptr != '{') return false;
		ptr++;

		while (*ptr) {
			if (isspace(*ptr)) {
				while (isspace(*ptr)) ptr++;
			}
			else if (*ptr == '}') {
				break;
			}
			else {
				pfc::string8 name, value;
				if (!parse_entity(ptr, name, value)) return false;
				if (!pfc::stricmp_ascii(name, "From")) {
					if (!pfc::string_is_numeric(value)) return false;
					link->from = pfc::atoui64_ex(value, ~0);
				}
				else if (!pfc::stricmp_ascii(name, "To")) {
					if (!pfc::string_is_numeric(value)) return false;
					link->to = pfc::atoui64_ex(value, ~0);
				}
				else if (!pfc::stricmp_ascii(name, "Smooth")) {
					if (!pfc::stricmp_ascii(value, "True") || !pfc::stricmp_ascii(value, "Yes")) {
						link->smooth = true;
					}
					else if (!pfc::stricmp_ascii(value, "False") || !pfc::stricmp_ascii(value, "No")) {
						link->smooth = false;
					}
					else {
						// parse error
						return false;
					}
				}
				else if (!pfc::stricmp_ascii(name, "Condition")) {
					link->condition = conds.parse(value);
					if (link->condition == nullptr) return false;
				}
				else if (!pfc::stricmp_ascii(name, "RefValue")) {
					if (!pfc::string_is_numeric(value)) return false;
					link->refvalue = value_clipper::clip_both(pfc::atoui_ex(value, ~0));
				}
				else if (!pfc::stricmp_ascii(name, "CondVar")) {
					if (!pfc::string_is_numeric(value)) return false;
					link->condvar = flag_clipper::clip_both(pfc::atoui_ex(value, ~0));
				}
				else {
					return false;
				}
			}
		}

		if (*ptr != '}') return false;
		ptr++;
		return true;
	}

	static bool parse_label(const char * & ptr, service_ptr_t<label> label) {
		// must point '{' , which indicates start of the block.
		if (*ptr != '{') return false;
		ptr++;

		while (*ptr) {
			if (isspace(*ptr)) {
				while (isspace(*ptr)) ptr++;
			}
			else if (*ptr == '}') {
				break;
			}
			else {
				pfc::string8 name, value;
				if (!parse_entity(ptr, name, value)) return false;
				if (!pfc::stricmp_ascii(name, "Position")) {
					if (!pfc::string_is_numeric(value)) return false;
					label->position = pfc::atoui64_ex(value, ~0);
				}
				else if (!pfc::stricmp_ascii(name, "Name")) {
					label->name.set_string(value);
				}
				else {
					return false;
				}
			}
		}

		if (*ptr != '}') return false;
		ptr++;
		return true;
	}

	static bool parse_label_formula(const char * p_formula, label_formula & p_out) {
		auto p = p_formula;
		// starts with '['
		if (*p != '[') return false;
		p++;
		t_size i = 0;
		while (pfc::char_is_numeric(p[i])) i++;
		if (i == 0) return false;
		p_out.flag = flag_clipper::clip_both(pfc::atoui_ex(p, i));
		p += i;
		// after flag, should be ']'
		if (*p != ']') return false;
		p++;
		auto sign = false;
		switch (*p) {
		case '=':
			p_out.oper = new formula_operator_set();
			break;
		case '+':
			sign = true;
			[[fallthrough]];
		case '-':
			if (*p == *(p + 1)) {
				if (sign)
					p_out.oper = new formula_operator_inc();
				else
					p_out.oper = new formula_operator_dec();
				p++;
				break;
			}
			else if (*(p + 1) == '=') {
				if (sign)
					p_out.oper = new formula_operator_add();
				else
					p_out.oper = new formula_operator_sub();
				p++;
				break;
			}
			[[fallthrough]];
		default:
			// unknown operator
			return false;
		}
		p++;
		if (!p_out.oper->require_operand) return true;
		p_out.indirect = false;
		if (*p == '[') {
			p_out.indirect = true;
			p++;
		}
		i = 0;
		while (pfc::char_is_numeric(p[i])) i++;
		if (i == 0) return false;
		if (p_out.indirect)
			p_out.value = flag_clipper::clip_both(pfc::atoui_ex(p, i));
		else
			p_out.value = value_clipper::clip_both(pfc::atoui_ex(p, i));
		p += i;
		if (p_out.indirect) {
			if (*p != ']') return false;
			p++;
		}
		return *p == 0;
	}
};

class loop_type_sli : public loop_type_impl_singleinput_base
{
private:
	using sli_value_t = int;
	input_decoder::ptr m_input;
	loop_event_point_list m_points;
	pfc::array_staticsize_t<sli_value_t> m_flags;
	bool m_no_flags;
	t_size m_crossfade_samples_half = 0;
	using sli = sli_processor<sli_value_t, 16, 0, 9999, loop_type_sli>;
	sli::loop_conditions sli_conds;
public:
	static const char * g_get_name() { return "kirikiri-SLI"; }
	static const char * g_get_short_name() { return "sli"; }
	static bool g_is_our_type(const char * type) { return !pfc::stringCompareCaseInsensitive(type, "sli"); }
	static bool g_is_explicit() { return true; }

	bool parse(const char * ptr) override {
		if (!*ptr) { return false; }
		m_points.remove_all();
		m_no_flags = true;
		if (*ptr != '#') {
			// v1
			auto p_length = strstr(ptr, "LoopLength=");
			auto p_start = strstr(ptr, "LoopStart=");
			if (!p_length || !p_start) return false;
			p_length += 11;
			p_start += 10;
			if (!pfc::char_is_numeric(*p_length) || !pfc::char_is_numeric(*p_start)) return false;
			const auto link = fb2k::service_new<sli::link_impl>();
			link->smooth = false;
			link->condition = sli_conds.parse("no");
			link->refvalue = 0;
			link->condvar = 0;
			const auto start = _atoi64(p_start);
			link->from = start + _atoi64(p_length);
			link->to = start;
			m_points.add_item(link);

			return true;
		}
		if (!pfc::strcmp_partial(ptr, "#2.00")) {
			// v2
			while (*ptr) {
				if (*ptr == '#') {
					// FIXME: original source checks only beginning-of-line...
					while (*ptr && *ptr != '\n') ptr++;
				}
				else if (isspace(*ptr)) {
					while (isspace(*ptr)) ptr++;
				}
				else if (pfc::stricmp_ascii(ptr, "Link") && !pfc::char_is_ascii_alpha(ptr[4])) {
					ptr += 4;
					while (isspace(*ptr)) ptr++;
					if (!*ptr) return false;
					const auto link = fb2k::service_new<sli::link_impl>();
					if (!sli::parse_link(ptr, sli_conds, link)) return false;
					if (m_no_flags && link->condition != nullptr && link->condition->is_valid)
						m_no_flags = false;
					m_points.add_item(link);
				}
				else if (pfc::stricmp_ascii(ptr, "Label") && !pfc::char_is_ascii_alpha(ptr[5])) {
					ptr += 5;
					while (isspace(*ptr)) ptr++;
					if (!*ptr) return false;
					const auto label = fb2k::service_new<sli::label>();
					if (!sli::parse_label(ptr, label)) return false;
					m_points.add_item(label);
				}
				else {
					return false;
				}
			}
			return true;
		}
		return false;
	}
	virtual t_size get_crossfade_samples_half() const { return m_crossfade_samples_half; }

	bool open_path_internal(file::ptr p_filehint, const char * path, t_input_open_reason /*p_reason*/, abort_callback & p_abort, bool p_from_redirect, bool p_skip_hints) override {
		open_path_helper(m_input, p_filehint, path, p_abort, p_from_redirect, p_skip_hints);
		switch_input(m_input, path, 0);
		return true;
	}

	void open_decoding_internal(t_uint32 subsong, t_uint32 flags, abort_callback & p_abort) override {
		m_crossfade_samples_half = MulDiv_Size(get_sample_rate(), 25, 1000);
		for (auto i = m_points.get_count() - 1; i != static_cast<t_size>(-1); i--) {
			service_ptr_t<sli_link> link;
			if (m_points[i]->service_query_t<sli_link>(link)) {
				if (link->set_smooth_samples(m_crossfade_samples_half)) {
					set_is_raw_supported(false);
					break;
				}
			}
		}
		switch_points(m_points);
		if (!m_no_flags) {
			m_flags.set_size_discard(sli::num_flags);
			pfc::fill_array_t(m_flags, 0);
		}
		loop_type_impl_singleinput_base::open_decoding_internal(subsong, flags, p_abort);
	}

	void get_info(t_uint32 subsong, file_info & p_info, abort_callback & p_abort) override {
		get_input()->get_info(subsong, p_info, p_abort);
		get_info_for_points(p_info, m_points, get_info_prefix(), get_sample_rate());
	}

	bool set_dynamic_info(file_info & p_out) override {
		loop_type_impl_base::set_dynamic_info(p_out);
		if (!m_no_flags) {
			pfc::string8 buf;
			const auto num = m_flags.get_size();
			for (t_size i = 0; i < num; i++)
				buf << "/[" << i << "]=" << m_flags[i];
			p_out.info_set("sli_flags", buf + 1);
		}
		return true;
	}

	bool check_condition(sli::link_impl & p_link) {
		if (m_no_flags || p_link.condition == nullptr || !p_link.condition->is_valid)
			return true; // invalid is always true
		return p_link.condition->check(m_flags[p_link.condvar], p_link.refvalue);
	}

	void process_formula(const char * p_formula) {
		if (m_no_flags) return;
		sli::label_formula formula;
		if (sli::parse_label_formula(p_formula, formula)) {
			const t_size flag = formula.flag;
			auto value = formula.value;
			if (formula.indirect) {
				value = m_flags[value];
			}
			m_flags[flag] = formula.oper->calculate(m_flags[flag], value);
		}
	}

	FB2K_MAKE_SERVICE_INTERFACE(loop_type_sli, loop_type);
};

// ReSharper disable once CppDeclaratorNeverUsed
static loop_type_factory_t<loop_type_sli> g_loop_type_sli;

// {5EEA84FA-6765-4917-A800-791AE10809E1}
FOOGUIDDECL const GUID sli_link::class_guid = 
{ 0x5eea84fa, 0x6765, 0x4917, { 0xa8, 0x0, 0x79, 0x1a, 0xe1, 0x8, 0x9, 0xe1 } };

// {E697CCC0-0ABF-46bd-BA8F-B19096765368}
FOOGUIDDECL const GUID loop_type_sli::class_guid = 
{ 0xe697ccc0, 0xabf, 0x46bd, { 0xba, 0x8f, 0xb1, 0x90, 0x96, 0x76, 0x53, 0x68 } };

class input_sli : public input_loop_base
{
public:
	input_sli() : input_loop_base("sli_") {}

	void open_internal(file::ptr p_filehint,const char * p_path,t_input_open_reason p_reason,abort_callback & p_abort) override {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		m_loopfile = p_filehint;
		m_path = p_path;
		input_open_file_helper(m_loopfile,p_path,p_reason,p_abort);//if m_file is null, opens file with appropriate privileges for our operation (read/write for writing tags, read-only otherwise).
		bool is_utf8;
		text_file_loader::read(m_loopfile,p_abort,m_loopcontent,is_utf8);
		pfc::string8 p_content_basepath;
		p_content_basepath.set_string(p_path, pfc::strlen_t(p_path) - 4); // .sli
		const loop_type_entry::ptr ptr = fb2k::service_new<loop_type_impl_t<loop_type_sli>>();
		loop_type::ptr instance = fb2k::service_new<loop_type_sli>();
		if (instance->parse(m_loopcontent) && instance->open_path(nullptr, p_content_basepath, p_reason, p_abort, true, false)) {
			m_loopentry = ptr;
			set_looptype(instance);
		} else {
			throw exception_io_data();
		}
	}

	static bool g_is_our_content_type(const char * /*p_content_type*/) {return false;}
	static bool g_is_our_path(const char * /*p_path*/,const char * p_extension) {return stricmp_utf8(p_extension, "sli") == 0;}

	static GUID g_get_guid() {
		// {8BDE8271-677F-4F33-8F3C-5800EFB15BCA}
		static constexpr GUID guid =
		{ 0x8bde8271, 0x677f, 0x4f33,{ 0x8f, 0x3c, 0x58, 0x0, 0xef, 0xb1, 0x5b, 0xca } };
		return guid;
	}
	static const char * g_get_name() {
		return "SLI Loop Information Handler";
	}
	static GUID g_get_preferences_guid() { return pfc::guid_null; }
	static bool g_is_low_merit() { return false; }
	static bool g_fallback_is_our_payload(const void* bytes, size_t bytesAvail, t_filesize bytesWhole) { return false; }
};


// ReSharper disable once CppDeclaratorNeverUsed
static input_singletrack_factory_t<input_sli, input_entry::flag_redirect | input_entry::flag_parallel_reads_slow> g_input_sli_factory;


//DECLARE_COMPONENT_VERSION("sli loop manager","0.3-dev",NULL);
DECLARE_FILE_TYPE_EX("SLI", "SLI Loop Information File","SLI Loop Information Files");
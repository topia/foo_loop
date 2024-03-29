#include "stdafx.h"
#include "looping.h"

using namespace loop_helper;

namespace {
	bool parse_looptype(const char * & p_content, pfc::string_base & p_type) {
		// first entity is type.
		pfc::string8 name, value;
		if (!parse_entity(p_content, name, value) || !!pfc::stringCompareCaseInsensitive(name, "type"))
			return false;
		p_type = value;
		return true;
	}
}

struct loop_type_prioritized_entry {
	t_uint8 priority = 0;
	loop_type_entry::ptr ptr;
};

template<typename t_item1, typename t_item2>
int looptype_priority_compare(const t_item1 & p_item1, const t_item2 & p_item2);

template<>
inline int looptype_priority_compare(const loop_type_prioritized_entry & p_item1, const loop_type_prioritized_entry & p_item2) {
	return pfc::compare_t(p_item1.priority, p_item2.priority);
}

class input_loop : public input_loop_base
{
public:
	input_loop() : input_loop_base("loop_") {}

	void open_internal(file::ptr p_filehint,const char * p_path,t_input_open_reason p_reason,abort_callback & p_abort) override {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		m_loopfile = p_filehint;
		m_path = p_path;
		input_open_file_helper(m_loopfile,p_path,p_reason,p_abort);//if m_file is null, opens file with appropriate privileges for our operation (read/write for writing tags, read-only otherwise).
		bool is_utf8;
		text_file_loader::read(m_loopfile,p_abort,m_loopcontent,is_utf8);
		const char * p_content = m_loopcontent;
		pfc::string8 looptype;
		parse_looptype(p_content, looptype);
		pfc::string8 p_content_basepath;
		p_content_basepath.set_string(p_path, pfc::strlen_t(p_path) - 5); // .loop
		service_enum_t<loop_type_entry> e;
		loop_type_entry::ptr ptr;
		pfc::list_t<loop_type_prioritized_entry> ents;
		const bool type_specified = !looptype.is_empty();
		while (e.next(ptr)) {
			// if type specified, use type only. otherwise try auto-probing
			if (type_specified ? ptr->is_our_type(looptype) : !ptr->is_explicit()) {
				loop_type_prioritized_entry ent = {100, ptr};
				loop_type_entry_v2::ptr v2ptr;
				if (ptr->service_query_t<loop_type_entry_v2>(v2ptr)) {
					ent.priority = v2ptr->get_priority();
				}
				ents.add_item(ent);
			}
		}
		if (ents.get_count() != 0) {
			pfc::array_staticsize_t<t_size> m_perm_by_prio(ents.get_count());
			order_helper::g_fill(m_perm_by_prio);
			ents.sort_get_permutation_t(looptype_priority_compare<loop_type_prioritized_entry, loop_type_prioritized_entry>, m_perm_by_prio.get_ptr());
			for (t_size i=0; i<m_perm_by_prio.get_size(); ++i) {
				if (m_looptype.is_empty()) {
					ptr = ents.get_item(m_perm_by_prio[i]).ptr;
					const loop_type::ptr instance = ptr->instantiate();
					if (instance->parse(p_content) && instance->open_path(nullptr, p_content_basepath, p_reason, p_abort, true, false)) {
						m_loopentry = ptr;
						set_looptype(instance);
						continue;
					}
				}
			}
			ents.remove_all();
		}

		if (m_looptype.is_empty()) {
			//console::formatter() << "loop parsing failed, resume to normal playback: \"" << file_path_display(p_path) << "\"";
			ptr = new service_impl_t<loop_type_impl_t<loop_type_entire>>();
			const loop_type::ptr instance = new service_impl_t<loop_type_entire>();
			if (instance->parse(p_content) && instance->open_path(nullptr, p_content_basepath, p_reason, p_abort, true, false)) {
				m_loopentry = ptr;
				set_looptype(instance);
			}
			PFC_ASSERT(m_looptype.is_valid()); // parse error on input_loop_type_entire !?
		}
	}

	static bool g_is_our_content_type(const char * /*p_content_type*/) {
		return false;
	}
	static bool g_is_our_path(const char * /*p_path*/,const char * p_extension) {
		return stricmp_utf8(p_extension, "loop") == 0;
	}

	static GUID g_get_guid() {
		// {14C848B3-899B-4B28-95A3-814CA84001FE}
		static constexpr GUID guid =
		{ 0x14c848b3, 0x899b, 0x4b28,{ 0x95, 0xa3, 0x81, 0x4c, 0xa8, 0x40, 0x1, 0xfe } };
		return guid;
	}
	static const char * g_get_name() {
		return "Standard Loop Information Handler";
	}
	static GUID g_get_preferences_guid() { return pfc::guid_null; }
	static bool g_is_low_merit() { return false; }
	static bool g_fallback_is_our_payload(const void* bytes, size_t bytesAvail, t_filesize bytesWhole) { return false;  }
};


// ReSharper disable once CppDeclaratorNeverUsed
static input_factory_t<input_loop, input_entry::flag_redirect> g_input_loop_factory;


DECLARE_COMPONENT_VERSION("Standard Loop Information Handler","0.5 alpha","Standard Looping Handler.\nThis includes .loop and .sli support.");
DECLARE_FILE_TYPE_EX("LOOP","Audio Loop Information File","Audio Loop Information Files");
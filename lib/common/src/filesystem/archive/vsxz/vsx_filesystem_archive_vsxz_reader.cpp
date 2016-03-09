#include <filesystem/archive/vsxz/vsx_filesystem_archive_vsxz_reader.h>
#include <tools/vsx_thread_pool.h>
#include <vsx_compression_lzma.h>
#include <vsx_compression_lzham.h>
#include <filesystem/tree/vsx_filesystem_tree_reader.h>
#include <filesystem/archive/vsxz/vsx_filesystem_archive_chunk_write.h>

namespace vsx
{

bool filesystem_archive_vsxz_reader::load(const char* archive_filename, bool load_data_multithreaded)
{
  mmap = filesystem_mmap::create(archive_filename);
  req_v(mmap, false);
  req_v(mmap->size > sizeof(vsxz_header) + sizeof(vsxz_header_file_info), false);
  header = (vsxz_header*)mmap->data;

  req_v(header->identifier[0] == 'V', false);
  req_v(header->identifier[1] == 'S', false);
  req_v(header->identifier[2] == 'X', false);
  req_v(header->identifier[3] == 'Z', false);

  uncompressed_data.allocate(header->compression_uncompressed_memory_size - 1);

  vsxz_header_chunk_info* chunk_info_table =
      (vsxz_header_chunk_info*) (
        mmap->data +
        sizeof(vsxz_header) +
        header->tree_size +
        sizeof(vsxz_header_file_info) * header->file_count
      );

  file_info_table = (vsxz_header_file_info*) (
        mmap->data +
        sizeof(vsxz_header) +
        header->tree_size
      );

  tree.initialize( mmap->data + sizeof(vsxz_header) );

  unsigned char* uncompressed_data_start = uncompressed_data.get_pointer();

  unsigned char* compressed_data_start =
      mmap->data +
      sizeof(vsxz_header) +
      header->tree_size +
      sizeof(vsxz_header_file_info) * header->file_count +
      sizeof(vsxz_header_chunk_info) * header->chunk_count
    ;

  uncompressed_data_start_pointers[0] = compressed_data_start;

  // first chunk is always uncompressed
  compressed_data_start += chunk_info_table[0].uncompressed_size;

  uint32_t offset_uncompressed = 0;
  uint32_t offset_compressed = 0;
  for (size_t i = 1; i < header->chunk_count; i++)
  {
    uncompressed_data_start_pointers[i] = compressed_data_start + offset_uncompressed;
    vsx_thread_pool::instance()->add(
      [=]
      (
        const vsxz_header_chunk_info& chunk,
        unsigned char* compressed_data,
        unsigned char* uncompressed_data
      )
      {
        vsx_ma_vector<unsigned char> compressed;
        compressed.set_data( compressed_data, chunk.compressed_size );
        vsx_ma_vector<unsigned char> uncompressed;
        uncompressed.set_data( uncompressed_data, chunk.uncompressed_size );

        if (chunk.compression_type == 1)
          compression_lzma::uncompress(uncompressed, compressed);

        if (chunk.compression_type == 2)
          compression_lzham::uncompress(uncompressed, compressed);
      },
      chunk_info_table[i],
      compressed_data_start + offset_compressed,
      uncompressed_data_start + offset_uncompressed
    );

    offset_compressed += chunk_info_table[i].compressed_size;
    offset_uncompressed += chunk_info_table[i].uncompressed_size;
  }
  vsx_thread_pool::instance()->wait_all();
  return true;
}

void filesystem_archive_vsxz_reader::file_open(const char* filename, file* &handle)
{
  uint32_t file_info_table_index = tree.get_payload_by_filename(vsx_string<>(filename));
  req(file_info_table_index);
  file_info_table_index--;

  vsxz_header_file_info* file_info = &file_info_table[file_info_table_index];

  unsigned char* data_ptr = uncompressed_data_start_pointers[file_info->chunk];

  handle->data.set_volatile();
  handle->data.set_data( data_ptr + file_info->offset, file_info->size );
}

void filesystem_archive_vsxz_reader::close()
{
  filesystem_mmap::destroy(mmap);
  header = 0x0;
  uncompressed_data.clear();
}


bool filesystem_archive_vsxz_reader::is_archive()
{
  return header != 0x0;
}


bool filesystem_archive_vsxz_reader::is_archive_populated()
{
  req_v(header, false);
  return header->file_count > 0;
}

bool filesystem_archive_vsxz_reader::is_file(vsx_string<> filename)
{
  return tree.get_payload_by_filename(vsx_string<>(filename)) != 0x0;
}

vsx_nw_vector<filesystem_archive_file_read>* filesystem_archive_vsxz_reader::files_get()
{
  return 0;
}

}

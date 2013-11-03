#include "pt3.h"

#define PT3_DMA_MAX_DESCS	204
#define PT3_DMA_PAGE_SIZE	(PT3_DMA_MAX_DESCS * sizeof(struct pt3_dma_desc))
#define PT3_DMA_BLOCK_COUNT	17
#define PT3_DMA_BLOCK_SIZE	(PT3_DMA_PAGE_SIZE * 47)
#define PT3_DMA_TS_BUF_SIZE	(PT3_DMA_BLOCK_SIZE * PT3_DMA_BLOCK_COUNT)
#define PT3_DMA_NOT_SYNC_BYTE	0x74

struct pt3_dma_desc {
	u64 page_addr;
	u32 page_size;
	u64 next_desc;
} __packed;

void pt3_dma_build_page_descriptor(struct pt3_dma *dma)
{
	struct pt3_dma_page *desc_info, *ts_info;
	u64 ts_addr, desc_addr;
	u32 i, j, ts_size, desc_remain, ts_info_pos, desc_info_pos;
	struct pt3_dma_desc *prev, *curr;

	pr_debug("#%d build page descriptor ts_count=%d ts_size=%d desc_count=%d desc_size=%d\n",
		dma->adap->idx, dma->ts_count, dma->ts_info[0].size, dma->desc_count, dma->desc_info[0].size);
	desc_info_pos = ts_info_pos = 0;
	desc_info = &dma->desc_info[desc_info_pos];
	desc_addr   = desc_info->addr;
	desc_remain = desc_info->size;
	desc_info->data_pos = 0;
	prev = NULL;
	curr = (struct pt3_dma_desc *)&desc_info->data[desc_info->data_pos];
	desc_info_pos++;

	for (i = 0; i < dma->ts_count; i++) {
		if (unlikely(ts_info_pos >= dma->ts_count)) {
			pr_debug("ts_info overflow max=%d curr=%d\n", dma->ts_count, ts_info_pos);
			return;
		}
		ts_info = &dma->ts_info[ts_info_pos];
		ts_addr = ts_info->addr;
		ts_size = ts_info->size;
		ts_info_pos++;
		pr_debug("#%d i=%d, ts_info addr=0x%llx ts_size=%d\n", dma->adap->idx, i, ts_addr, ts_size);
		for (j = 0; j < ts_size / PT3_DMA_PAGE_SIZE; j++) {
			if (desc_remain < sizeof(struct pt3_dma_desc)) {
				if (unlikely(desc_info_pos >= dma->desc_count)) {
					pr_debug("#%d desc_info overflow max=%d curr=%d\n",
						dma->adap->idx, dma->desc_count, desc_info_pos);
					return;
				}
				desc_info = &dma->desc_info[desc_info_pos];
				desc_info->data_pos = 0;
				curr = (struct pt3_dma_desc *)&desc_info->data[desc_info->data_pos];
				pr_debug("#%d desc_info_pos=%d ts_addr=0x%llx remain=%d\n",
					dma->adap->idx, desc_info_pos, ts_addr, desc_remain);
				desc_addr = desc_info->addr;
				desc_remain = desc_info->size;
				desc_info_pos++;
			}
			if (prev)
				prev->next_desc = desc_addr | 0b10;
			curr->page_addr = ts_addr           | 0b111;
			curr->page_size = PT3_DMA_PAGE_SIZE | 0b111;
			curr->next_desc = 0b10;
			pr_debug("#%d j=%d dma write desc ts_addr=0x%llx desc_info_pos=%d desc_remain=%d\n",
				dma->adap->idx, j, ts_addr, desc_info_pos, desc_remain);
			ts_addr += PT3_DMA_PAGE_SIZE;

			prev = curr;
			desc_info->data_pos += sizeof(struct pt3_dma_desc);
			if (unlikely(desc_info->data_pos > desc_info->size)) {
				pr_debug("#%d dma desc_info data overflow max=%d curr=%d\n",
					dma->adap->idx, desc_info->size, desc_info->data_pos);
				return;
			}
			curr = (struct pt3_dma_desc *)&desc_info->data[desc_info->data_pos];
			desc_addr += sizeof(struct pt3_dma_desc);
			desc_remain -= sizeof(struct pt3_dma_desc);
		}
	}
	if (prev)
		prev->next_desc = dma->desc_info->addr | 0b10;
}

struct pt3_dma *pt3_dma_create(struct pt3_adapter *adap)
{
	struct pt3_dma_page *page;
	u32 i;

	struct pt3_dma *dma = kzalloc(sizeof(struct pt3_dma), GFP_KERNEL);
	if (!dma) {
		pr_debug("#%d fail allocate PT3_DMA\n", adap->idx);
		goto fail;
	}
	dma->adap = adap;
	dma->enabled = false;
	mutex_init(&dma->lock);

	dma->ts_count = PT3_DMA_BLOCK_COUNT;
	dma->ts_info = kzalloc(sizeof(struct pt3_dma_page) * dma->ts_count, GFP_KERNEL);
	if (!dma->ts_info) {
		pr_debug("#%d fail allocate TS DMA page\n", adap->idx);
		goto fail;
	}
	pr_debug("#%d Alloc TS buf (ts_count %d)\n", adap->idx, dma->ts_count);
	for (i = 0; i < dma->ts_count; i++) {
		page = &dma->ts_info[i];
		page->size = PT3_DMA_BLOCK_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(adap->pt3->pdev, page->size, &page->addr);
		if (!page->data) {
			pr_debug("#%d fail alloc_consistent. %d\n", adap->idx, i);
			goto fail;
		}
	}

	dma->desc_count = 1 + (PT3_DMA_TS_BUF_SIZE / PT3_DMA_PAGE_SIZE - 1) / PT3_DMA_MAX_DESCS;
	dma->desc_info = kzalloc(sizeof(struct pt3_dma_page) * dma->desc_count, GFP_KERNEL);
	if (!dma->desc_info) {
		pr_debug("#%d fail allocate Desc DMA page\n", adap->idx);
		goto fail;
	}
	pr_debug("#%d Alloc Descriptor buf (desc_count %d)\n", adap->idx, dma->desc_count);
	for (i = 0; i < dma->desc_count; i++) {
		page = &dma->desc_info[i];
		page->size = PT3_DMA_PAGE_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(adap->pt3->pdev, page->size, &page->addr);
		if (!page->data) {
			pr_debug("#%d fail alloc_consistent %d\n", adap->idx, i);
			goto fail;
		}
	}

	pr_debug("#%d build page descriptor\n", adap->idx);
	pt3_dma_build_page_descriptor(dma);
	return dma;
fail:
	if (dma)
		pt3_dma_free(dma);
	return NULL;
}

void pt3_dma_free(struct pt3_dma *dma)
{
	struct pt3_dma_page *page;
	u32 i;

	if (dma->ts_info) {
		for (i = 0; i < dma->ts_count; i++) {
			page = &dma->ts_info[i];
			if (page->data)
				pci_free_consistent(dma->adap->pt3->pdev, page->size, page->data, page->addr);
		}
		kfree(dma->ts_info);
	}
	if (dma->desc_info) {
		for (i = 0; i < dma->desc_count; i++) {
			page = &dma->desc_info[i];
			if (page->data)
				pci_free_consistent(dma->adap->pt3->pdev, page->size, page->data, page->addr);
		}
		kfree(dma->desc_info);
	}
	kfree(dma);
}

void __iomem *pt3_dma_get_base_addr(struct pt3_dma *dma)
{
	return dma->adap->pt3->i2c->reg[0] + REG_BASE + (0x18 * dma->adap->idx);
}

void pt3_dma_reset(struct pt3_dma *dma)
{
	struct pt3_dma_page *page;
	u32 i;

	for (i = 0; i < dma->ts_count; i++) {
		page = &dma->ts_info[i];
		memset(page->data, 0, page->size);
		page->data_pos = 0;
		*page->data = PT3_DMA_NOT_SYNC_BYTE;
	}
	dma->ts_pos = 0;
}

void pt3_dma_set_enabled(struct pt3_dma *dma, bool enabled)
{
	void __iomem *base = pt3_dma_get_base_addr(dma);
	u64 start_addr = dma->desc_info->addr;

	if (enabled) {
		pr_debug("#%d DMA enable start_addr=%llx\n", dma->adap->idx, start_addr);
		pt3_dma_reset(dma);
		writel(1 << 1, base + REG_DMA_CTL);
		writel(PT3_SHIFT_MASK(start_addr,  0, 32), base + REG_DMA_DESC_L);
		writel(PT3_SHIFT_MASK(start_addr, 32, 32), base + REG_DMA_DESC_H);
		pr_debug("set descriptor address low %llx\n",  PT3_SHIFT_MASK(start_addr,  0, 32));
		pr_debug("set descriptor address high %llx\n", PT3_SHIFT_MASK(start_addr, 32, 32));
		writel(1 << 0, base + REG_DMA_CTL);
	} else {
		pr_debug("#%d DMA disable\n", dma->adap->idx);
		writel(1 << 1, base + REG_DMA_CTL);
		while (1) {
			if (!PT3_SHIFT_MASK(readl(base + REG_STATUS), 0, 1))
				break;
			msleep_interruptible(1);
		}
	}
	dma->enabled = enabled;
}

/* convert Gray code to binary, e.g. 1001 -> 1110 */
static u32 pt3_dma_gray2binary(u32 gray, u32 bit)
{
	u32 binary = 0, i, j, k;

	for (i = 0; i < bit; i++) {
		k = 0;
		for (j = i; j < bit; j++)
			k = k ^ PT3_SHIFT_MASK(gray, j, 1);
		binary |= k << i;
	}
	return binary;
}

u32 pt3_dma_get_ts_error_packet_count(struct pt3_dma *dma)
{
	return pt3_dma_gray2binary(readl(pt3_dma_get_base_addr(dma) + REG_TS_ERR), 32);
}

void pt3_dma_set_test_mode(struct pt3_dma *dma, enum pt3_dma_mode mode, u16 initval)
{
	void __iomem *base = pt3_dma_get_base_addr(dma);
	u32 data = mode | initval;
	pr_debug("set_test_mode base=%p data=0x%04x\n", base, data);
	writel(data, base + REG_TS_CTL);
}

bool pt3_dma_ready(struct pt3_dma *dma)
{
	struct pt3_dma_page *page;
	u8 *p;

	u32 next = dma->ts_pos + 1;
	if (next >= dma->ts_count)
		next = 0;
	page = &dma->ts_info[next];
	p = &page->data[page->data_pos];

	if (*p == 0x47)
		return true;
	if (*p == PT3_DMA_NOT_SYNC_BYTE)
		return false;

	pr_debug("#%d invalid sync byte value=0x%02x ts_pos=%d data_pos=%d curr=0x%02x\n",
		dma->adap->idx, *p, next, page->data_pos, dma->ts_info[dma->ts_pos].data[0]);
	return false;
}

ssize_t pt3_dma_copy(struct pt3_dma *dma, struct dvb_demux *demux, loff_t *ppos)
{
	bool ready;
	struct pt3_dma_page *page;
	u32 i, prev;
	size_t csize, remain = dma->ts_info[dma->ts_pos].size;

	mutex_lock(&dma->lock);
	pr_debug("#%d dma_copy ts_pos=0x%x data_pos=0x%x ppos=0x%x\n",
		   dma->adap->idx, dma->ts_pos, dma->ts_info[dma->ts_pos].data_pos, (int)(*ppos));
	for (;;) {
		for (i = 0; i < 20; i++) {
			ready = pt3_dma_ready(dma);
			if (ready)
				break;
			msleep_interruptible(30);
		}
		if (!ready) {
			pr_debug("#%d dma_copy NOT READY\n", dma->adap->idx);
			goto last;
		}
		prev = dma->ts_pos - 1;
		if (prev < 0 || dma->ts_count <= prev)
			prev = dma->ts_count - 1;
		if (dma->ts_info[prev].data[0] != PT3_DMA_NOT_SYNC_BYTE)
			pr_debug("#%d DMA buffer overflow. prev=%d data=0x%x\n",
					dma->adap->idx, prev, dma->ts_info[prev].data[0]);
		page = &dma->ts_info[dma->ts_pos];
		for (;;) {
			csize = (remain < (page->size - page->data_pos)) ?
				 remain : (page->size - page->data_pos);
			dvb_dmx_swfilter(demux, &page->data[page->data_pos], csize);
			*ppos += csize;
			remain -= csize;
			page->data_pos += csize;
			if (page->data_pos >= page->size) {
				page->data_pos = 0;
				page->data[page->data_pos] = PT3_DMA_NOT_SYNC_BYTE;
				dma->ts_pos++;
				if (dma->ts_pos >= dma->ts_count)
					dma->ts_pos = 0;
				break;
			}
			if (remain <= 0)
				goto last;
		}
	}
last:
	mutex_unlock(&dma->lock);
	return dma->ts_info[dma->ts_pos].size - remain;
}

u32 pt3_dma_get_status(struct pt3_dma *dma)
{
	return readl(pt3_dma_get_base_addr(dma) + REG_STATUS);
}


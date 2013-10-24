#define PT3_DMA_DESC_SIZE	20
#define PT3_DMA_PAGE_SIZE	4096
#define PT3_DMA_MAX_DESCS	204	/* 4096 / 20 */
#define PT3_DMA_BLOCK_COUNT	(17)
#define PT3_DMA_BLOCK_SIZE	(PT3_DMA_PAGE_SIZE * 47)
#define PT3_DMA_TS_BUF_SIZE	(PT3_DMA_BLOCK_SIZE * PT3_DMA_BLOCK_COUNT)
#define PT3_DMA_NOT_SYNC_BYTE	0x74

static void pt3_dma_link_descriptor(u64 next_addr, u8 *desc)
{
	(*(u64 *)(desc + 12)) = next_addr | 2;
}

static void pt3_dma_write_descriptor(u64 ts_addr, u32 size, u64 next_addr, u8 *desc)
{
	(*(u64 *)(desc +  0)) = ts_addr   | 7;
	(*(u32 *)(desc +  8)) = size      | 7;
	(*(u64 *)(desc + 12)) = next_addr | 2;
}

void pt3_dma_build_page_descriptor(struct pt3_dma *dma, bool loop)
{
	struct pt3_dma_page *desc_info, *ts_info;
	u64 ts_addr, desc_addr;
	u32 i, j, ts_size, desc_remain, ts_info_pos, desc_info_pos;
	u8 *prev, *curr;

	if (unlikely(!dma)) {
		PT3_PRINTK(KERN_ALERT, "dma build page descriptor needs DMA\n");
		return;
	}
	PT3_PRINTK(KERN_DEBUG, "#%d build page descriptor ts_count=%d ts_size=0x%x desc_count=%d desc_size=0x%x\n",
		dma->adap->idx, dma->ts_count, dma->ts_info[0].size, dma->desc_count, dma->desc_info[0].size);
	desc_info_pos = ts_info_pos = 0;
	desc_info = &dma->desc_info[desc_info_pos];
	if (unlikely(!desc_info)) {
		PT3_PRINTK(KERN_ALERT, "dma maybe failed allocate desc_info %d\n", desc_info_pos);
		return;
	}
	desc_addr = desc_info->addr;
	desc_remain = desc_info->size;
	desc_info->data_pos = 0;
	prev = NULL;
	curr = &desc_info->data[desc_info->data_pos];
	if (unlikely(!curr)) {
		PT3_PRINTK(KERN_ALERT, "dma maybe failed allocate desc_info->data %d\n",
			desc_info_pos);
		return;
	}
	desc_info_pos++;

	for (i = 0; i < dma->ts_count; i++) {
		if (unlikely(dma->ts_count <= ts_info_pos)) {
			PT3_PRINTK(KERN_ALERT, "ts_info overflow max=%d curr=%d\n",
				dma->ts_count, ts_info_pos);
			return;
		}
		ts_info = &dma->ts_info[ts_info_pos];
		if (unlikely(!ts_info)) {
			PT3_PRINTK(KERN_ALERT, "dma maybe failed allocate ts_info %d\n",
				ts_info_pos);
			return;
		}
		ts_addr = ts_info->addr;
		ts_size = ts_info->size;
		ts_info_pos++;
		PT3_PRINTK(KERN_DEBUG, "#%d ts_info addr=0x%llx size=0x%x\n", dma->adap->idx, ts_addr, ts_size);
		if (unlikely(!ts_info)) {
			PT3_PRINTK(KERN_ALERT, "dma maybe failed allocate ts_info %d\n",
				ts_info_pos);
			return;
		}
		for (j = 0; j < ts_size / PT3_DMA_PAGE_SIZE; j++) {
			if (desc_remain < PT3_DMA_DESC_SIZE) {
				if (unlikely(dma->desc_count <= desc_info_pos)) {
					PT3_PRINTK(KERN_ALERT, "desc_info overflow max=%d curr=%d\n",
						dma->desc_count, desc_info_pos);
					return;
				}
				desc_info = &dma->desc_info[desc_info_pos];
				desc_info->data_pos = 0;
				curr = &desc_info->data[desc_info->data_pos];
				if (unlikely(!curr)) {
					PT3_PRINTK(KERN_ALERT, "dma maybe failed allocate desc_info->data %d\n",
						desc_info_pos);
					return;
				}
				PT3_PRINTK(KERN_DEBUG, "#%d desc_info_pos=%d ts_addr=0x%llx remain=%d\n",
					dma->adap->idx, desc_info_pos, ts_addr, desc_remain);
				desc_addr = desc_info->addr;
				desc_remain = desc_info->size;
				desc_info_pos++;
			}
			if (prev)
				pt3_dma_link_descriptor(desc_addr, prev);
			pt3_dma_write_descriptor(ts_addr, PT3_DMA_PAGE_SIZE, 0, curr);
			PT3_PRINTK(KERN_DEBUG, "#%d dma write desc ts_addr=0x%llx desc_info_pos=%d\n",
				dma->adap->idx, ts_addr, desc_info_pos);
			ts_addr += PT3_DMA_PAGE_SIZE;

			prev = curr;
			desc_info->data_pos += PT3_DMA_DESC_SIZE;
			if (unlikely(desc_info->size <= desc_info->data_pos)) {
				PT3_PRINTK(KERN_ALERT, "dma desc_info data overflow.\n");
				return;
			}
			curr = &desc_info->data[desc_info->data_pos];
			desc_addr += PT3_DMA_DESC_SIZE;
			desc_remain -= PT3_DMA_DESC_SIZE;
		}
	}

	if (prev) {
		if (loop)
			pt3_dma_link_descriptor(dma->desc_info->addr, prev);
		else
			pt3_dma_link_descriptor(1, prev);
	}
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

struct pt3_dma *pt3_dma_create(struct pt3_adapter *adap)
{
	struct pt3_dma_page *page;
	u32 i;

	struct pt3_dma *dma = kzalloc(sizeof(struct pt3_dma), GFP_KERNEL);
	if (!dma) {
		PT3_PRINTK(KERN_ALERT, "fail allocate PT3_DMA\n");
		goto fail;
	}
	dma->adap = adap;
	dma->enabled = false;
	mutex_init(&dma->lock);

	dma->ts_count = PT3_DMA_BLOCK_COUNT;
	dma->ts_info = kzalloc(sizeof(struct pt3_dma_page) * dma->ts_count, GFP_KERNEL);
	if (!dma->ts_info) {
		PT3_PRINTK(KERN_ALERT, "fail allocate PT3_DMA_PAGE\n");
		goto fail;
	}
	for (i = 0; i < dma->ts_count; i++) {
		page = &dma->ts_info[i];
		page->size = PT3_DMA_BLOCK_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(adap->pt3->pdev, page->size, &page->addr);
		if (!page->data) {
			PT3_PRINTK(KERN_ALERT, "fail allocate consistent. %d\n", i);
			goto fail;
		}
	}
	PT3_PRINTK(KERN_DEBUG, "Allocate TS buffer.\n");

	dma->desc_count = (PT3_DMA_TS_BUF_SIZE / (PT3_DMA_PAGE_SIZE) + PT3_DMA_MAX_DESCS - 1) / PT3_DMA_MAX_DESCS;
	dma->desc_info = kzalloc(sizeof(struct pt3_dma_page) * dma->desc_count, GFP_KERNEL);
	if (!dma->desc_info) {
		PT3_PRINTK(KERN_ALERT, "fail allocate PT3_DMA_PAGE\n");
		goto fail;
	}
	for (i = 0; i < dma->desc_count; i++) {
		page = &dma->desc_info[i];
		page->size = PT3_DMA_PAGE_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(adap->pt3->pdev, page->size, &page->addr);
		if (!page->data) {
			PT3_PRINTK(KERN_ALERT, "fail allocate consistent. %d\n", i);
			goto fail;
		}
	}
	PT3_PRINTK(KERN_DEBUG, "Allocate Descriptor buffer.\n");

	pt3_dma_build_page_descriptor(dma, true);
	PT3_PRINTK(KERN_DEBUG, "set page descriptor.\n");
	return dma;
fail:
	if (dma)
		pt3_dma_free(dma);
	return NULL;
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
		PT3_PRINTK(KERN_DEBUG, "#%d DMA enable start_addr=%llx\n", dma->adap->idx, start_addr);
		pt3_dma_reset(dma);
		writel(1 << 1, base + REG_DMA_CTL);
		writel(PT3_SHIFT_MASK(start_addr,  0, 32), base + REG_DMA_DESC_L);
		writel(PT3_SHIFT_MASK(start_addr, 32, 32), base + REG_DMA_DESC_H);
		PT3_PRINTK(KERN_DEBUG, "set descriptor address low %llx\n",
			PT3_SHIFT_MASK(start_addr,  0, 32));
		PT3_PRINTK(KERN_DEBUG, "set descriptor address high %llx\n",
			PT3_SHIFT_MASK(start_addr, 32, 32));
		writel(1 << 0, base + REG_DMA_CTL);
	} else {
		PT3_PRINTK(KERN_DEBUG, "#%d DMA disable\n", dma->adap->idx);
		writel(1 << 1, base + REG_DMA_CTL);
		while (1) {
			if (!PT3_SHIFT_MASK(readl(base + REG_STATUS), 0, 1))
				break;
			msleep_interruptible(1);
		}
	}
	dma->enabled = enabled;
}

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

enum pt3_dma_mode {
	USE_LFSR = 1 << 16,
	REVERSE  = 1 << 17,
	RESET    = 1 << 18,
};

void pt3_dma_set_test_mode(struct pt3_dma *dma, enum pt3_dma_mode mode, u16 initval)
{
	void __iomem *base = pt3_dma_get_base_addr(dma);
	u32 data = mode | initval;
	PT3_PRINTK(KERN_DEBUG, "set_test_mode base=%p data=0x%04x\n", base, data);
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

	PT3_PRINTK(KERN_DEBUG, "invalid sync byte value=0x%02x ts_pos=%d data_pos=%d curr=0x%02x\n",
			*p, next, page->data_pos, dma->ts_info[dma->ts_pos].data[0]);
	return false;
}

ssize_t pt3_dma_copy(struct pt3_dma *dma, struct dvb_demux *demux, loff_t *ppos)
{
	bool ready;
	struct pt3_dma_page *page;
	u32 i, prev;
	size_t csize, remain = dma->ts_info[dma->ts_pos].size;

	mutex_lock(&dma->lock);
	PT3_PRINTK(KERN_DEBUG, "#%d dma_copy ts_pos=0x%x data_pos=0x%x ppos=0x%x\n",
		   dma->adap->idx, dma->ts_pos, dma->ts_info[dma->ts_pos].data_pos, (int)(*ppos));
	for (;;) {
		for (i = 0; i < 20; i++) {
			ready = pt3_dma_ready(dma);
			if (ready)
				break;
			msleep_interruptible(30);
		}
		if (!ready) {
			PT3_PRINTK(KERN_DEBUG, "#%d dma_copy NOT READY\n", dma->adap->idx);
			goto last;
		}
		prev = dma->ts_pos - 1;
		if (prev < 0 || dma->ts_count <= prev)
			prev = dma->ts_count - 1;
		if (dma->ts_info[prev].data[0] != PT3_DMA_NOT_SYNC_BYTE)
			PT3_PRINTK(KERN_INFO, "#%d DMA buffer overflow. prev=%d data=0x%x\n",
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


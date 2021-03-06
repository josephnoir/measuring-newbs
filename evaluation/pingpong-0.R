library(tikzDevice)
library(ggplot2)
require(RColorBrewer)
require(gridExtra)

pprelordudp <- read.csv("evaluation/pingpong/reliable_ordered_udp-0.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

pprelordudp$avg <- rowMeans(pprelordudp[,2:11])
pprelordudp$sdev <- apply(pprelordudp[,2:11], 1, sd)
pprelordudp$proto <- 'Reliable Ordered UDP'

ppreludp <- read.csv("evaluation/pingpong/reliable_udp-0.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

ppreludp$avg <- rowMeans(ppreludp[,2:11])
ppreludp$sdev <- apply(ppreludp[,2:11], 1, sd)
ppreludp$proto <- 'Reliable UDP'

pptcp <- read.csv("evaluation/pingpong/tcp-0.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

pptcp$avg <- rowMeans(pptcp[,2:11])
pptcp$sdev <- apply(pptcp[,2:11], 1, sd)
pptcp$proto <- 'TCP'

ppdf <- rbind(pptcp, ppreludp)
ppdf <- rbind(ppdf, pprelordudp)
ppdf$upper <- ppdf$avg + ppdf$sdev
ppdf$lower <- ppdf$avg - ppdf$sdev

pp_plot <- ggplot(ppdf, aes(x=loss, y=avg/1000, color=proto)) +
           geom_line() + # size=0.8) +
           geom_point(aes(shape=proto), size = 2, stroke=0.8) +
           geom_errorbar(
             mapping=aes(
               ymin=lower/1000,
               ymax=upper/1000
             ),
             width=0.2
           ) +
           scale_shape_manual(values=c(1, 4, 3)) +
           scale_x_continuous(breaks=seq(0, 10, 1)) + # expand=c(0, 0), limits=c(0, 10)
           scale_y_continuous(limits=c(0, 40), breaks=seq(0, 40, 10)) + # expand=c(0, 0), limits=c(0, 10)
           theme_bw() +
           theme(
             legend.title=element_blank(),
             legend.key=element_rect(fill='white'), 
             legend.background=element_rect(fill="white", colour="black", size=0.25),
             legend.direction="vertical",
             legend.justification=c(0, 1),
             legend.position=c(0, 1),
             legend.box.margin=margin(c(3, 3, 3, 3)),
             legend.key.size=unit(0.8, 'lines'),
             text=element_text(size=9),
             strip.background=element_blank(),
             strip.text.x=element_blank()
           ) +
           # scale_color_grey() +
           scale_color_brewer(type="qual", palette=6) +
           labs(x="Relative Loss on Link [%]", y="Runtime [s]")

tikz(file="figs/pingpong-0.tikz", sanitize=TRUE, width=3.4, height=2.3)
pp_plot
dev.off()
ggsave("figs/pingpong-0.pdf", plot=pp_plot, width=3.4, height=2.3)



